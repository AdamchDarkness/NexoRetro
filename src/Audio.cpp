#include "Audio.hpp"

#include <algorithm>
#include <cmath>

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
}

double Audio::queuedSeconds() const
{
    if (!m_stream)
        return 0.0;
    return SDL_GetAudioStreamQueued(m_stream) / (kBytesPerFrame * m_sampleRate);
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
