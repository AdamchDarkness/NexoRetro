#pragma once
// VoiceChat : le chat vocal du salon.
//
// Chaque PC enregistre son micro par morceaux de 20 ms, les compresse avec
// Opus (le codec de Discord et des appels vidéo : ~60 octets par morceau)
// et les envoie par la Session, sans garantie d'arrivée : un morceau en
// retard ne sert plus à rien. L'hôte relaie la voix de chacun aux autres.
//
// À la réception, chaque personne qui parle a son propre "flux" SDL, que la
// carte son mélange avec le jeu. Un morceau perdu est reconstitué par Opus
// (grâce à une copie de secours glissée dans le morceau suivant, "FEC").
//
// Trois modes : désactivé, micro ouvert (on n'envoie que quand on parle,
// d'après le volume), ou appuyer pour parler (une touche à maintenir).

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;
struct Settings;
namespace net {
class Session;
}

class VoiceChat {
public:
    VoiceChat() = default;
    ~VoiceChat();
    VoiceChat(const VoiceChat&) = delete;
    VoiceChat& operator=(const VoiceChat&) = delete;

    // À chaque image. talkKeyHeld : la touche "appuyer pour parler" est enfoncée.
    void update(net::Session& session, const Settings& settings, bool talkKeyHeld);

    // Couper son propre micro (les autres ne vous entendent plus).
    void setSelfMuted(bool muted) { m_selfMuted = muted; }
    bool selfMuted() const { return m_selfMuted; }
    // Ne plus entendre un membre du salon.
    void setMemberMuted(uint8_t id, bool muted);
    bool memberMuted(uint8_t id) const { return m_mutedMembers.count(id) != 0; }

    // Qui parle en ce moment (soi-même compris, avec son propre numéro).
    bool talking(uint8_t id) const;
    // Volume du micro, 0 à 1 (pour la jauge des Paramètres).
    float micLevel() const { return m_level; }
    // Le seuil du micro ouvert, sur la même échelle que micLevel().
    static float threshold(int sensitivity);
    bool micOpen() const { return m_mic != nullptr; }
    const std::string& micError() const { return m_micError; }

    // Tests automatiques : ne jamais ouvrir le vrai micro du PC.
    void setMicAllowed(bool allowed) { m_micAllowed = allowed; }

    // Paramètres : écouter son propre micro pour le régler.
    void setTesting(bool testing) { m_testing = testing; }
    bool testing() const { return m_testing; }

private:
    struct Speaker {
        OpusDecoder* decoder = nullptr;
        SDL_AudioStream* out = nullptr;
        uint16_t lastSeq = 0;
        bool started = false;
        Uint64 lastPacket = 0;
    };

    bool openMic();
    void closeMic();
    void closeAll();
    bool openEncoder();
    Speaker& speaker(uint8_t id);
    void play(uint8_t id, const std::string& data, float gain);
    void capture(net::Session* session, const Settings& settings, bool transmit);

    SDL_AudioStream* m_mic = nullptr;
    std::string m_micError;
    Uint64 m_micRetryAt = 0;
    OpusEncoder* m_encoder = nullptr;
    uint16_t m_seq = 0;
    int m_hangover = 0;     // morceaux encore envoyés après la fin de la voix
    Uint64 m_selfTalking = 0;
    float m_level = 0.0f;
    bool m_selfMuted = false;
    bool m_testing = false;
    bool m_micAllowed = true;
    uint8_t m_selfId = 0;
    std::map<uint8_t, Speaker> m_speakers;
    std::set<uint8_t> m_mutedMembers;

    // Tests automatiques (EMU_VOICE_TEST=tone : un "la" à la place du micro ;
    // EMU_VOICE_DUMP=fichier : la voix reçue, en PCM 48 kHz mono).
    bool m_testTone = false;
    double m_tonePhase = 0.0;
    SDL_IOStream* m_dump = nullptr;
};
