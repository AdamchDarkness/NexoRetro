#include "VoiceChat.hpp"

#include "Settings.hpp"
#include "net/Bytes.hpp"
#include "net/Session.hpp"

#include <opus.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRate = 48000;
constexpr int kFrame = 960;          // 20 ms à 48 kHz
constexpr Uint64 kTalkingMs = 300;   // "parle" s'il a envoyé de la voix il y a moins de 0,3 s
constexpr Uint64 kPhraseGapMs = 300; // plus long sans voix : la phrase suivante repart à zéro
constexpr int kHangoverFrames = 15;  // micro ouvert : on continue 0,3 s après la fin de la voix
constexpr uint8_t kLoopback = 255;   // "personne" qui joue sa propre voix (test du micro)

const SDL_AudioSpec kSpec = {SDL_AUDIO_S16, 1, kRate};

// Volume d'un morceau, de 0 (silence) à 1 (très fort), sur une échelle en
// décibels (-60 dB -> 0) : c'est ainsi que l'oreille perçoit le volume.
float levelOf(const int16_t* pcm, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(pcm[i]) * pcm[i];
    const double rms = std::sqrt(sum / count) / 32768.0;
    if (rms <= 0.0)
        return 0.0f;
    return static_cast<float>(std::clamp((20.0 * std::log10(rms) + 60.0) / 60.0, 0.0, 1.0));
}

} // namespace

VoiceChat::~VoiceChat()
{
    closeAll();
    if (m_dump)
        SDL_CloseIO(m_dump);
}

float VoiceChat::threshold(int sensitivity)
{
    // Sensibilité 0 : il faut parler fort (-24 dB) ; 100 : un murmure passe (-51 dB).
    return 0.6f - 0.45f * std::clamp(sensitivity, 0, 100) / 100.0f;
}

void VoiceChat::setMemberMuted(uint8_t id, bool muted)
{
    if (muted)
        m_mutedMembers.insert(id);
    else
        m_mutedMembers.erase(id);
}

bool VoiceChat::talking(uint8_t id) const
{
    const Uint64 now = SDL_GetTicks();
    if (id == m_selfId)
        return m_selfTalking && now - m_selfTalking < kTalkingMs;
    const auto it = m_speakers.find(id);
    return it != m_speakers.end() && !memberMuted(id) && now - it->second.lastPacket < kTalkingMs;
}

// --- Micro -------------------------------------------------------------------------

bool VoiceChat::openMic()
{
    // Le micro par défaut de Windows, en mono 48 kHz : SDL convertit si le
    // micro travaille autrement.
    m_mic = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &kSpec, nullptr, nullptr);
    if (!m_mic) {
        m_micError = std::string("Micro introuvable : ") + SDL_GetError();
        SDL_Log("Chat vocal : %s", m_micError.c_str());
        return false;
    }
    SDL_ResumeAudioStreamDevice(m_mic);
    m_micError.clear();
    SDL_Log("Chat vocal : micro ouvert");
    return true;
}

void VoiceChat::closeMic()
{
    if (m_mic)
        SDL_DestroyAudioStream(m_mic);
    m_mic = nullptr;
    m_level = 0.0f;
    m_hangover = 0;
}

void VoiceChat::closeAll()
{
    closeMic();
    for (auto& [id, s] : m_speakers) {
        if (s.out)
            SDL_DestroyAudioStream(s.out);
        if (s.decoder)
            opus_decoder_destroy(s.decoder);
    }
    m_speakers.clear();
    if (m_encoder)
        opus_encoder_destroy(m_encoder);
    m_encoder = nullptr;
}

bool VoiceChat::openEncoder()
{
    if (m_encoder)
        return true;
    int error = 0;
    m_encoder = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &error);
    if (!m_encoder)
        return false;
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(24000));     // 24 kbit/s : une voix claire
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));      // copie de secours du morceau précédent
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    return true;
}

// Lit le micro (ou le son de test) par morceaux de 20 ms, et envoie ceux
// qui doivent l'être.
void VoiceChat::capture(net::Session* session, const Settings& settings, bool talkKeyHeld)
{
    const float gate = threshold(settings.voiceSensitivity);
    int16_t pcm[kFrame];
    auto frame = [&](bool pushToTalk) {
        m_level = levelOf(pcm, kFrame);
        if (m_level >= gate)
            m_hangover = kHangoverFrames;
        else if (m_hangover > 0)
            --m_hangover;
        if (m_testing) {
            Speaker& self = speaker(kLoopback);
            SDL_PutAudioStreamData(self.out, pcm, sizeof(pcm));
        }
        const bool send = pushToTalk ? talkKeyHeld : m_hangover > 0;
        if (!send || !session || !openEncoder())
            return;
        unsigned char packet[400];
        const opus_int32 n = opus_encode(m_encoder, pcm, kFrame, packet, sizeof(packet));
        if (n <= 0)
            return;
        net::ByteWriter w;
        w.u16(m_seq++).raw(std::string_view(reinterpret_cast<const char*>(packet), static_cast<size_t>(n)));
        session->sendVoice(w.data());
        m_selfTalking = SDL_GetTicks();
    };

    const bool pushToTalk = settings.voiceMode == 2 && !m_testing;
    if (m_testTone) {
        // Test : un "la" (440 Hz) en continu, comme si on parlait sans arrêt.
        static Uint64 start = SDL_GetTicks();
        static Uint64 produced = 0;
        const Uint64 due = (SDL_GetTicks() - start) / 20;
        for (; produced < due; ++produced) {
            for (int i = 0; i < kFrame; ++i) {
                pcm[i] = static_cast<int16_t>(8000.0 * std::sin(m_tonePhase));
                m_tonePhase += 2.0 * 3.14159265358979 * 440.0 / kRate;
            }
            frame(false);
        }
        return;
    }
    if (!m_mic)
        return;
    while (SDL_GetAudioStreamAvailable(m_mic) >= static_cast<int>(sizeof(pcm)))
        if (SDL_GetAudioStreamData(m_mic, pcm, sizeof(pcm)) == static_cast<int>(sizeof(pcm)))
            frame(pushToTalk);
}

// --- Haut-parleurs -------------------------------------------------------------------

VoiceChat::Speaker& VoiceChat::speaker(uint8_t id)
{
    Speaker& s = m_speakers[id];
    if (!s.out) {
        // Un flux par personne : la carte son les mélange avec le son du jeu.
        s.out = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kSpec, nullptr, nullptr);
        if (s.out)
            SDL_ResumeAudioStreamDevice(s.out);
    }
    if (!s.decoder && id != kLoopback) {
        int error = 0;
        s.decoder = opus_decoder_create(kRate, 1, &error);
    }
    return s;
}

void VoiceChat::play(uint8_t id, const std::string& data, float gain)
{
    if (data.size() < 3)
        return;
    Speaker& s = speaker(id);
    if (!s.out || !s.decoder)
        return;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const uint16_t seq = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
    const unsigned char* opus = bytes + 2;
    const opus_int32 size = static_cast<opus_int32>(data.size() - 2);
    const Uint64 now = SDL_GetTicks();
    int16_t pcm[kFrame];
    auto push = [&](int samples) {
        if (samples <= 0)
            return;
        SDL_PutAudioStreamData(s.out, pcm, samples * 2);
        if (m_dump)
            SDL_WriteIO(m_dump, pcm, static_cast<size_t>(samples) * 2);
    };
    auto silence = [&](int ms) {
        std::fill(std::begin(pcm), std::end(pcm), int16_t(0));
        for (int done = 0; done < ms; done += 20)
            SDL_PutAudioStreamData(s.out, pcm, sizeof(pcm));
    };

    // Début d'une phrase : un peu d'avance (60 ms de silence), pour que les
    // morceaux qui arrivent irrégulièrement ne fassent pas de trous.
    if (!s.started || now - s.lastPacket > kPhraseGapMs) {
        SDL_ClearAudioStream(s.out);
        silence(60);
        s.started = true;
        s.lastSeq = static_cast<uint16_t>(seq - 1);
    }
    // [C++] La différence de deux uint16_t ramenée en int16_t : le compteur
    // repart à 0 après 65535 sans fausser la comparaison.
    const int gap = static_cast<int16_t>(seq - s.lastSeq);
    if (gap <= 0)
        return; // doublon, ou arrivé après le suivant : trop tard
    if (gap > 1 && gap <= 4) {
        // Morceaux perdus : Opus les imagine (les derniers grâce à la copie
        // de secours contenue dans celui-ci).
        for (int i = 0; i < gap - 2; ++i)
            push(opus_decode(s.decoder, nullptr, 0, pcm, kFrame, 0));
        push(opus_decode(s.decoder, opus, size, pcm, kFrame, 1));
    }
    push(opus_decode(s.decoder, opus, size, pcm, kFrame, 0));
    s.lastSeq = seq;
    s.lastPacket = now;

    // Trop de retard accumulé (plus d'un quart de seconde) : on rattrape.
    if (SDL_GetAudioStreamQueued(s.out) > kRate * 2 / 4) {
        SDL_ClearAudioStream(s.out);
        silence(40);
    }
    SDL_SetAudioStreamGain(s.out, gain);
}

// --- Chaque image ----------------------------------------------------------------------

void VoiceChat::update(net::Session& session, const Settings& settings, bool talkKeyHeld)
{
    static bool envChecked = false;
    if (!envChecked) {
        envChecked = true;
        if (const char* test = SDL_getenv("EMU_VOICE_TEST"))
            m_testTone = std::string(test) == "tone";
        if (const char* dump = SDL_getenv("EMU_VOICE_DUMP"))
            m_dump = SDL_IOFromFile(dump, "wb");
    }

    const bool inSalon = session.inSalon();
    m_selfId = session.selfId();
    const bool enabled = inSalon && settings.voiceMode != 0;
    const bool micWanted = (enabled && !m_selfMuted) || m_testing;
    const Uint64 now = SDL_GetTicks();
    if (micWanted && !m_mic && !m_testTone && m_micAllowed && now >= m_micRetryAt && !openMic())
        m_micRetryAt = now + 5000; // pas de micro : on réessaie dans 5 s (il vient peut-être d'être branché)
    if (!micWanted && m_mic)
        closeMic();
    capture(enabled && !m_selfMuted ? &session : nullptr, settings, talkKeyHeld);
    if (!m_testing && m_speakers.count(kLoopback)) {
        SDL_DestroyAudioStream(m_speakers[kLoopback].out);
        m_speakers.erase(kLoopback);
    }

    // La voix des autres.
    const float gain = std::clamp(settings.voiceVolume, 0, 200) / 100.0f;
    for (const net::Session::FramePacket& v : session.takeVoice())
        if (enabled && !memberMuted(v.from) && v.from != m_selfId)
            play(v.from, v.data, gain);

    // Hors du salon : on libère tout (le micro, les haut-parleurs).
    if (!inSalon && !m_testing && (!m_speakers.empty() || m_encoder)) {
        closeAll();
        m_mutedMembers.clear();
    }
}
