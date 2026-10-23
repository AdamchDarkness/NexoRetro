#pragma once
// Session : un salon privé entre plusieurs émulateurs, sans serveur central.
//
// L'émulateur de l'hôte EST le salon (topologie "en étoile") : chaque ami
// se connecte à l'hôte, qui relaie le chat, la liste des membres et le jeu
// choisi à tout le monde.
//
// Pour rejoindre :
//   1. L'hôte ouvre un port UDP, découvre son adresse publique (STUN),
//      demande à sa box d'ouvrir le port (UPnP), puis fabrique un code.
//   2. L'ami colle le code (et le mot de passe) : son émulateur envoie des
//      "Bonjour" à toutes les adresses du code, plusieurs fois par seconde.
//   3. Le premier "Bonjour" qui passe gagne : l'hôte vérifie le mot de
//      passe et répond "Bienvenue" (ou "Refusé").
//   4. Si la box de l'hôte bloque tout, l'ami lui renvoie un "code de
//      réponse" : l'hôte envoie alors à son tour des paquets vers l'ami, et
//      chaque box laisse passer une conversation qu'elle croit avoir
//      commencée. C'est la "perforation de NAT".
//
// Les messages importants (chat, liste des membres, jeu choisi...) sont
// "fiables" : renvoyés jusqu'à ce que le destinataire en accuse réception.
//
// Tout se passe dans update(), appelée à chaque image : aucun thread ici.

#include "InviteCode.hpp"
#include "Socket.hpp"
#include "Stun.hpp"
#include "Upnp.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace net {

// Où en est un membre avec le jeu choisi par l'hôte.
enum class GameStatus : uint8_t {
    Unknown,   // pas encore de jeu choisi, ou pas encore de réponse
    Checking,  // calcul de l'empreinte en cours
    Ready,     // il a exactement le même fichier
    Missing,   // il n'a pas ce jeu
    Different, // il a ce jeu, mais pas la même version du fichier
};

// Le jeu choisi par l'hôte. L'empreinte identifie le fichier exact.
struct SalonGame {
    uint64_t hash = 0;
    uint64_t size = 0;
    std::string systemId;
    std::string title;
    // Mode câble link : chacun joue SON jeu de cette console (avec sa
    // sauvegarde), reliés par le câble. Sinon : tous le même fichier.
    bool link = false;
};

struct Member {
    uint8_t id = 0; // 0 = l'hôte
    std::string name;
    int pingMs = -1;
    int lossPct = -1; // paquets perdus sur les dernières secondes, en % (-1 = pas encore mesuré)
    GameStatus status = GameStatus::Unknown;
    bool host = false;
    bool self = false;
};

// La qualité d'une connexion, d'après le ping et les pertes.
enum class LinkQuality { Unknown, Excellent, Good, Fair, Poor };
LinkQuality qualityOf(int pingMs, int lossPct);
const char* qualityLabel(LinkQuality quality); // "Excellente", "Bonne"...

class Session {
public:
    enum class State { Idle, Preparing, Hosting, Joining, InSalon };
    static constexpr int kMaxMembers = 8; // hôte compris

    struct Event {
        enum class Type { Joined, MemberJoined, MemberLeft, Chat, GameChanged, Closed, Info, Error, AppMessage };
        Type type;
        std::string text;
        std::string author; // pour Chat
        uint8_t from = 0;   // pour AppMessage : numéro du membre qui l'envoie
        std::string data;   // pour AppMessage : le contenu
    };

    // Un paquet "d'images" (touches du netplay) : rapide, sans garantie.
    struct FramePacket {
        uint8_t from = 0;
        std::string data;
    };

    Session();
    ~Session();

    void setNickname(std::string name) { m_nickname = std::move(name); }

    // Mot de passe vide = salon ouvert à quiconque a le code.
    bool host(const std::string& salonName, const std::string& password);
    bool join(const std::string& code, const std::string& password);
    // Côté hôte : le code de réponse envoyé par un ami (si la box bloque).
    bool acceptAnswer(const std::string& code);
    void leave();

    void update();
    std::vector<Event> takeEvents();

    void sendChat(const std::string& text);
    void selectGame(const SalonGame& game); // hôte uniquement
    void kick(uint8_t memberId);            // hôte uniquement
    // Le résultat de la vérification du jeu sur CE PC.
    void setLocalStatus(GameStatus status);

    // --- Pour le netplay ---------------------------------------------------
    // Message fiable pour un membre précis (hôte), ou pour l'hôte (membre).
    // to = -1 : tous les membres (hôte) / l'hôte (membre).
    void sendApp(int to, const std::string& data);
    // Paquet rapide, sans garantie d'arrivée (les touches de chaque image).
    void sendFrame(int to, const std::string& data);
    std::vector<FramePacket> takeFrames();
    uint8_t selfId() const { return m_selfId; }
    int pingOf(uint8_t memberId) const;
    int lossOf(uint8_t memberId) const; // en %, -1 si inconnu

    // --- Pour le chat vocal ------------------------------------------------
    // Un morceau de voix (20 ms compressés), sans garantie d'arrivée : à
    // tous les autres (l'hôte relaie ceux des membres).
    void sendVoice(const std::string& data);
    std::vector<FramePacket> takeVoice(); // from = numéro de celui qui parle

    // --- Pour l'interface ----------------------------------------------
    State state() const { return m_state; }
    bool inSalon() const { return m_state == State::Hosting || m_state == State::InSalon; }
    bool isHost() const { return m_isHost; }
    const std::string& salonName() const { return m_salonName; }
    const std::string& inviteCode() const { return m_inviteCode; } // hôte, une fois prêt (code court)
    const std::string& fullInviteCode() const { return m_fullInviteCode; } // toutes les adresses
    const std::string& answerCode() const { return m_answerCode; } // ami, après quelques secondes
    const std::vector<Member>& members() const { return m_members; }
    const std::optional<SalonGame>& game() const { return m_game; }
    GameStatus localStatus() const { return m_localStatus; }
    float secondsInState() const { return (SDL_GetTicks() - m_stateSince) / 1000.0f; }
    const std::vector<std::string>& details() const { return m_details; } // lignes d'état techniques

private:
    // Types de paquets. Les messages "fiables" voyagent dans Reliable.
    enum class Packet : uint8_t { Hello = 1, Welcome, Reject, Punch, Ping, Pong, Reliable, Ack, Bye, Frame, Voice };
    enum class Kind : uint8_t { Chat = 1, Roster, Game, Status, Kick, App };

    // Une liaison avec un autre émulateur (fiabilité + ping).
    struct Link {
        Address addr;
        struct Outgoing {
            std::string packet;
            Uint64 lastSent = 0;
        };
        uint32_t nextSeq = 1;
        std::map<uint32_t, Outgoing> unacked;
        std::set<uint32_t> received;
        Uint64 lastReceived = 0;
        Uint64 lastPing = 0;
        int pingMs = -1;
        // Pertes : les derniers pings envoyés, et ceux qui ont eu leur réponse.
        static constexpr int kPingHistory = 32;
        uint32_t pingSeq = 0;
        Uint64 pingSent[kPingHistory] = {};
        bool pingAnswered[kPingHistory] = {};
        int lossPct = -1;
    };
    struct Peer {
        uint8_t id = 0;
        uint32_t nonce = 0; // identifie l'ami même s'il arrive par plusieurs chemins
        std::string name;
        GameStatus status = GameStatus::Unknown;
        Link link;
    };

    bool openSocket(bool lanPort); // lanPort : l'un des ports connus du réseau local (hôte)
    void setState(State s);
    void startStun();
    void updatePreparation();
    std::vector<Address> ownCandidates() const;
    Address bestAddress() const; // celle du code court

    std::string packet(Packet type, const std::string& payload = {}) const;
    void send(const Address& to, Packet type, const std::string& payload = {});
    void sendReliable(Link& link, Kind kind, const std::string& data);
    void serviceLink(Link& link, Uint64 now);
    Link* linkFrom(const Address& from);

    void handlePacket(const Address& from, const uint8_t* data, size_t size);
    void handleHello(const Address& from, const std::string& payload);
    void handleMessage(const Address& from, Kind kind, const std::string& data, uint32_t seq);
    void removePeer(uint8_t id, const std::string& reason);
    void closeSalon(const std::string& reason);

    // Hôte : prévenir tout le monde.
    void broadcastRoster();
    std::string gameMessage() const;
    void rebuildMembers();

    void event(Event::Type type, std::string text, std::string author = {});
    void detail(std::string line);

    UdpSocket m_socket;
    State m_state = State::Idle;
    Uint64 m_stateSince = 0;
    bool m_isHost = false;
    uint32_t m_sessionId = 0;
    uint64_t m_passwordHash = 0;
    std::string m_nickname = "Joueur";
    std::string m_salonName;

    // Préparation : adresse publique (STUN) et ouverture de port (UPnP).
    std::optional<StunRequest> m_stun;
    std::vector<Address> m_stunServers;
    Uint64 m_stunLastSend = 0;
    int m_stunAttempts = 0;
    std::optional<Address> m_publicAddress;
    bool m_stunDone = false;
    std::unique_ptr<PortMapping> m_upnp;
    bool m_upnpReported = false;

    std::string m_inviteCode;
    std::string m_fullInviteCode;
    std::string m_answerCode;
    std::vector<Address> m_targets; // adresses à "percer"
    std::vector<Address> m_lanTargets; // ami : diffusion sur les réseaux locaux
    Uint64 m_lastProbe = 0;

    // Hôte : les amis connectés.
    std::map<uint8_t, Peer> m_peers;
    Uint64 m_lastRoster = 0;
    Uint64 m_lastMembersRefresh = 0;
    // Membre : la liaison avec l'hôte.
    Link m_hostLink;
    uint32_t m_nonce = 0;
    uint8_t m_selfId = 0;
    uint32_t m_lastRosterSeq = 0;
    uint32_t m_lastGameSeq = 0;

    std::vector<Member> m_members;
    std::optional<SalonGame> m_game;
    GameStatus m_localStatus = GameStatus::Unknown;

    std::vector<Event> m_events;
    std::vector<FramePacket> m_frames;
    std::vector<FramePacket> m_voice;
    std::vector<std::string> m_details;
};

} // namespace net
