#include "Audio.hpp"

#include "VideoRecorder.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kBytesPerFrame = 2 * sizeof(int16_t); // stéréo, 16 bits
}

Audio::~Audio()
{
    if (m_stream)
        SDL_DestroyAudioStream(m_stream);
}

bool Audio::open(double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    if (m_stream && sampleRate == m_sampleRate)
        return true;
    if (m_stream)
        SDL_DestroyAudioStream(m_stream);

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(std::lround(sampleRate));

    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_stream) {
        SDL_Log("Ouverture audio impossible : %s", SDL_GetError());
        return false;
    }
    m_sampleRate = sampleRate;
    m_extra = 0.0; // (nouveau jeu : on repart du tampon réglé)
    m_ratio = 1.0f;
    SDL_SetAudioStreamGain(m_stream, m_gain);
    // Le périphérique s'ouvre en pause : on le démarre.
    SDL_ResumeAudioStreamDevice(m_stream);
    return true;
}

void Audio::setVolume(int percent)
{
    // SDL multiplie lui-même chaque échantillon par ce "gain".
    m_gain = static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
    if (m_stream)
        SDL_SetAudioStreamGain(m_stream, m_gain);
}

void Audio::push(const int16_t* frames, size_t count)
{
    if (m_stream && !m_muted)
        SDL_PutAudioStreamData(m_stream, frames, static_cast<int>(count * kBytesPerFrame));
    if (m_recorder && !m_muted)
        m_recorder->addAudio(frames, count);
}

double Audio::queuedSeconds() const
{
    if (!m_stream)
        return 0.0;
    return SDL_GetAudioStreamQueued(m_stream) / (kBytesPerFrame * m_sampleRate);
}

void Audio::setTargetLatency(double seconds)
{
    m_baseTarget = std::clamp(seconds, 0.02, 0.5);
}

void Audio::setMinimumLatency(double seconds)
{
    m_minimum = std::clamp(seconds, 0.0, 0.5);
}

void Audio::regulate()
{
    if (!m_stream || m_muted)
        return;
    const double queued = queuedSeconds();
    const double goal = target();
    // Beaucoup trop de son en attente (après une pause, un chargement) :
    // on repart de zéro plutôt que d'entendre le jeu en retard.
    if (queued > goal * 3.0 + 0.1) {
        SDL_ClearAudioStream(m_stream);
        return;
    }
    // File presque vide : un trou s'entend. Trois en cinq secondes, et la
    // file grandit de 20 ms (jusqu'à 200 ms de plus).
    const Uint64 now = SDL_GetTicks();
    if (queued < 0.008) {
        // Un seul petit silence pour reconstituer la file, plutôt qu'une
        // série de micro-coupures (le "grésillement") image après image.
        const size_t frames = static_cast<size_t>(goal * 0.5 * m_sampleRate);
        const std::vector<int16_t> silence(frames * 2, 0);
        SDL_PutAudioStreamData(m_stream, silence.data(), static_cast<int>(frames * kBytesPerFrame));
        if (now - m_underrunWindow > 5000) {
            m_underrunWindow = now;
            m_underruns = 0;
        }
        if (++m_underruns >= 3 && m_extra < 0.2) {
            m_extra += 0.02;
            m_underruns = 0;
            SDL_Log("Son : tampon agrandi à %d ms (le jeu prend parfois du retard)", static_cast<int>(target() * 1000));
        }
    }
    // EMU_AUDIO_LOG=1 : l'état de la file une fois par seconde (tests).
    static const bool log = SDL_getenv("EMU_AUDIO_LOG") != nullptr;
    static Uint64 lastLog = 0;
    if (log && now - lastLog >= 1000) {
        lastLog = now;
        SDL_Log("AUDIO : file %d ms, visé %d ms, vitesse %.4f", static_cast<int>(queued * 1000), static_cast<int>(goal * 1000),
                static_cast<double>(m_ratio));
    }
    // Plus de son que visé : lecture un peu plus rapide ; moins : un peu plus lente.
    const double error = std::clamp((queued - goal) / goal, -1.0, 1.0);
    const float ratio = static_cast<float>(1.0 + error * 0.005);
    if (std::abs(ratio - m_ratio) > 0.0002f) {
        m_ratio = ratio;
        SDL_SetAudioStreamFrequencyRatio(m_stream, m_ratio);
    }
}

unsigned Audio::occupancy() const
{
    const double fill = queuedSeconds() / (target() * 2.0);
    return static_cast<unsigned>(std::clamp(fill, 0.0, 1.0) * 100.0);
}

bool Audio::underrunLikely() const
{
    return queuedSeconds() < target() * 0.25;
}

void Audio::clear()
{
    if (m_stream)
        SDL_ClearAudioStream(m_stream);
}

void Audio::setPaused(bool paused)
{
    if (!m_stream)
        return;
    if (paused) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_ClearAudioStream(m_stream);
    } else {
        SDL_ResumeAudioStreamDevice(m_stream);
    }
}
