#include "VideoRecorder.hpp"

#include "Utf8.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// Media Foundation : l'encodeur vidéo et son intégrés à Windows.
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

namespace {

constexpr int kAudioRate = 48000;
constexpr int kVideoHeight = 720;       // 720p : net, et léger à encoder
constexpr UINT32 kVideoBitrate = 8000000; // 8 Mbit/s : les pixels restent nets
constexpr size_t kMaxQueuedFrames = 90; // au-delà (PC trop lent), des images sont sautées

// [Windows] Les objets COM se libèrent avec Release() ; ce petit gardien le
// fait tout seul à la fin du bloc (le RAII, comme std::unique_ptr).
template <typename T>
struct Com {
    T* p = nullptr;
    ~Com()
    {
        if (p)
            p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

// Mise à l'échelle de l'image du jeu dans la vidéo, proportions gardées
// (bandes noires si besoin). Point le plus proche : les pixels restent nets.
void scaleInto(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh)
{
    std::memset(dst, 0, static_cast<size_t>(dw) * dh * 4);
    const double scale = std::min(static_cast<double>(dw) / sw, static_cast<double>(dh) / sh);
    const int w = std::max(1, static_cast<int>(sw * scale));
    const int h = std::max(1, static_cast<int>(sh * scale));
    const int x0 = (dw - w) / 2;
    const int y0 = (dh - h) / 2;
    std::vector<int> columns(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x)
        columns[static_cast<size_t>(x)] = std::min(sw - 1, static_cast<int>(x / scale));
    for (int y = 0; y < h; ++y) {
        // L'image arrive de bas en haut (OpenGL) : on la remet à l'endroit.
        const int sy = sh - 1 - std::min(sh - 1, static_cast<int>(y / scale));
        const uint32_t* line = reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(sy) * sw * 4);
        uint32_t* out = reinterpret_cast<uint32_t*>(dst + (static_cast<size_t>(y0 + y) * dw + x0) * 4);
        for (int x = 0; x < w; ++x)
            out[x] = line[columns[static_cast<size_t>(x)]] | 0xFF000000u;
    }
}

} // namespace

VideoRecorder::~VideoRecorder()
{
    stop();
}

bool VideoRecorder::start(const fs::path& file, double fps, double sampleRate)
{
    stop();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    m_file = file;
    m_fps = fps > 1.0 ? fps : 60.0;
    m_width = 0;
    m_height = 0;
    m_error.clear();
    m_stopping = false;
    m_jobs.clear();
    // Le son du core (32 040 Hz pour la Super Nintendo, 44 100...) est converti en 48 kHz.
    const SDL_AudioSpec from{SDL_AUDIO_S16, 2, static_cast<int>(std::lround(sampleRate > 0 ? sampleRate : kAudioRate))};
    const SDL_AudioSpec to{SDL_AUDIO_S16, 2, kAudioRate};
    m_resampler = SDL_CreateAudioStream(&from, &to);
    m_recording = true;
    m_thread = std::thread([this] { worker(); });
    SDL_Log("Enregistrement vidéo : %s", toUtf8(file).c_str());
    return true;
}

void VideoRecorder::stop()
{
    if (!m_thread.joinable())
        return;
    // Le reste du son converti, puis fin du thread (qui termine le fichier).
    if (m_resampler) {
        SDL_FlushAudioStream(m_resampler);
        addAudio(nullptr, 0);
    }
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    m_thread.join();
    m_recording = false;
    if (m_resampler)
        SDL_DestroyAudioStream(m_resampler);
    m_resampler = nullptr;
    SDL_Log("Enregistrement vidéo terminé : %s", toUtf8(m_file).c_str());
}

void VideoRecorder::addFrame(std::vector<uint8_t> bgra, int width, int height, uint64_t frame)
{
    if (!m_recording || width <= 0 || height <= 0)
        return;
    std::lock_guard lock(m_mutex);
    size_t queued = 0;
    for (const Job& j : m_jobs)
        queued += j.video ? 1 : 0;
    if (queued >= kMaxQueuedFrames)
        return; // (l'encodeur ne suit pas : on saute cette image plutôt que d'avaler la mémoire)
    m_jobs.push_back({true, std::move(bgra), width, height, frame});
    m_wake.notify_one();
}

void VideoRecorder::addAudio(const int16_t* frames, size_t count)
{
    if (!m_recording || !m_resampler)
        return;
    if (frames && count > 0)
        SDL_PutAudioStreamData(m_resampler, frames, static_cast<int>(count * 4));
    const int available = SDL_GetAudioStreamAvailable(m_resampler);
    if (available <= 0)
        return;
    Job job;
    job.data.resize(static_cast<size_t>(available));
    const int got = SDL_GetAudioStreamData(m_resampler, job.data.data(), available);
    if (got <= 0)
        return;
    job.data.resize(static_cast<size_t>(got));
    std::lock_guard lock(m_mutex);
    m_jobs.push_back(std::move(job));
    m_wake.notify_one();
}

std::string VideoRecorder::takeError()
{
    std::lock_guard lock(m_mutex);
    std::string e;
    e.swap(m_error);
    return e;
}

void VideoRecorder::worker()
{
    // [Windows] Media Foundation est fait d'objets COM : chaque thread qui
    // s'en sert doit "initialiser COM" (et le libérer en partant).
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    auto fail = [&](const char* what, HRESULT hr) {
        char text[128];
        std::snprintf(text, sizeof(text), "Enregistrement vidéo impossible (%s, erreur 0x%08lX)", what,
                      static_cast<unsigned long>(hr));
        SDL_Log("%s", text);
        std::lock_guard lock(m_mutex);
        m_error = text;
    };

    Com<IMFSinkWriter> writer;
    DWORD videoStream = 0;
    DWORD audioStream = 0;
    bool ready = false;
    bool broken = false;
    std::vector<uint8_t> scaled;
    uint64_t firstFrame = 0;
    LONGLONG audioTime = 0; // en unités de 100 ns
    LONGLONG lastVideoTime = -1;

    // Le fichier est créé à la première image : elle donne les proportions de la vidéo.
    auto open = [&](int srcW, int srcH) -> bool {
        m_height = kVideoHeight;
        m_width = static_cast<int>(std::lround(kVideoHeight * static_cast<double>(srcW) / srcH)) & ~1; // (pair, pour H.264)
        m_width = std::clamp(m_width, 320, 1920);
        Com<IMFAttributes> attributes;
        MFCreateAttributes(&attributes, 1);
        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        HRESULT hr = MFCreateSinkWriterFromURL(m_file.c_str(), nullptr, attributes, &writer);
        if (FAILED(hr))
            return fail("création du fichier", hr), false;

        // La vidéo : H.264 à la sortie, images BGRA à l'entrée (Windows convertit).
        const UINT32 fpsNum = static_cast<UINT32>(std::lround(m_fps * 1000));
        Com<IMFMediaType> videoOut;
        MFCreateMediaType(&videoOut);
        videoOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        videoOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        videoOut->SetUINT32(MF_MT_AVG_BITRATE, kVideoBitrate);
        videoOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(videoOut, MF_MT_FRAME_SIZE, static_cast<UINT32>(m_width), static_cast<UINT32>(m_height));
        MFSetAttributeRatio(videoOut, MF_MT_FRAME_RATE, fpsNum, 1000);
        MFSetAttributeRatio(videoOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->AddStream(videoOut, &videoStream);
        if (FAILED(hr))
            return fail("flux vidéo", hr), false;
        Com<IMFMediaType> videoIn;
        MFCreateMediaType(&videoIn);
        videoIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        videoIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        videoIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        videoIn->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(m_width * 4)); // lignes de haut en bas
        MFSetAttributeSize(videoIn, MF_MT_FRAME_SIZE, static_cast<UINT32>(m_width), static_cast<UINT32>(m_height));
        MFSetAttributeRatio(videoIn, MF_MT_FRAME_RATE, fpsNum, 1000);
        MFSetAttributeRatio(videoIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = writer->SetInputMediaType(videoStream, videoIn, nullptr);
        if (FAILED(hr))
            return fail("format des images", hr), false;

        // Le son : AAC 192 kbit/s à la sortie, PCM 16 bits stéréo 48 kHz à l'entrée.
        Com<IMFMediaType> audioOut;
        MFCreateMediaType(&audioOut);
        audioOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        audioOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        audioOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        audioOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
        audioOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        audioOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
        hr = writer->AddStream(audioOut, &audioStream);
        if (FAILED(hr))
            return fail("flux audio", hr), false;
        Com<IMFMediaType> audioIn;
        MFCreateMediaType(&audioIn);
        audioIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        audioIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        audioIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        audioIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
        audioIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        audioIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        audioIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
        hr = writer->SetInputMediaType(audioStream, audioIn, nullptr);
        if (FAILED(hr))
            return fail("format du son", hr), false;

        hr = writer->BeginWriting();
        if (FAILED(hr))
            return fail("début de l'écriture", hr), false;
        scaled.resize(static_cast<size_t>(m_width) * m_height * 4);
        return true;
    };

    // Un morceau de données -> un "échantillon" Media Foundation, daté.
    auto write = [&](DWORD stream, const uint8_t* data, size_t size, LONGLONG time, LONGLONG duration) {
        Com<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(size), &buffer)))
            return false;
        BYTE* dst = nullptr;
        buffer->Lock(&dst, nullptr, nullptr);
        std::memcpy(dst, data, size);
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(size));
        Com<IMFSample> sample;
        MFCreateSample(&sample);
        sample->AddBuffer(buffer);
        sample->SetSampleTime(time);
        sample->SetSampleDuration(duration);
        const HRESULT hr = writer->WriteSample(stream, sample);
        if (FAILED(hr))
            fail("écriture", hr);
        return SUCCEEDED(hr);
    };

    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stopping || !m_jobs.empty(); });
            if (m_jobs.empty())
                break; // (arrêt demandé, et tout est écrit)
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        if (broken)
            continue;
        if (!ready) {
            if (!job.video)
                continue; // (le son qui précède la première image est ignoré)
            ready = open(job.width, job.height);
            broken = !ready;
            if (broken)
                continue;
            firstFrame = job.frame;
        }
        if (job.video) {
            // Date de l'image : son numéro dans le jeu, à la cadence du jeu.
            const LONGLONG time = static_cast<LONGLONG>(std::llround((job.frame - firstFrame) * 10000000.0 / m_fps));
            if (time <= lastVideoTime)
                continue; // (même image d'émulation affichée deux fois)
            scaleInto(job.data.data(), job.width, job.height, scaled.data(), m_width, m_height);
            const LONGLONG duration = static_cast<LONGLONG>(10000000.0 / m_fps);
            broken = !write(videoStream, scaled.data(), scaled.size(), time, duration);
            lastVideoTime = time;
        } else {
            const LONGLONG duration = static_cast<LONGLONG>(job.data.size() / 4) * 10000000LL / kAudioRate;
            broken = !write(audioStream, job.data.data(), job.data.size(), audioTime, duration);
            audioTime += duration;
        }
    }
    if (ready && writer)
        writer->Finalize(); // (écrit l'index du MP4 : sans lui, la vidéo ne se lit pas)
    if (writer.p) // (avant MFShutdown : plus aucun objet Media Foundation ne doit vivre)
        writer.p->Release();
    writer.p = nullptr;
    MFShutdown();
    CoUninitialize();
}
