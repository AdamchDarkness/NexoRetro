#pragma once
// LinkPlay : le mode "câble link" du jeu en ligne.
//
// Le netplay (Netplay.hpp) fait tourner UNE console partagée : chacun tient
// une manette de la même machine. Le câble link, c'est l'inverse : chaque
// joueur a SA console, SON jeu et SA sauvegarde (Pokémon Rubis d'un côté,
// Saphir de l'autre), et seules les données du câble voyagent.
//
// Deux façons de transporter ces données, selon le core :
//  - Netpacket (gpSP pour la GBA) : le core nous donne ses paquets, on les
//    fait passer par le salon (messages fiables, l'hôte relaie) ;
//  - GameLink (Gambatte pour la Game Boy) : le core ouvre lui-même une
//    connexion TCP vers un "autre Gambatte". Sur chaque PC, un relais
//    (LinkTunnel) se fait passer pour cet autre Gambatte et transporte les
//    octets jusqu'au PC de l'ami. Ici, on échange seulement les adresses
//    des deux relais.
//
// Déroulement : 'k' l'hôte lance (liste des joueurs) -> chacun démarre SON
// jeu et répond 'l' -> l'hôte donne le départ 'g' -> paquets 'p' (ou le
// relais) -> 'x' fin de partie.

#include "Session.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace net {

class LinkPlay {
public:
    enum class Phase { Idle, Loading, Running };
    enum class Transport : uint8_t { Netpacket = 0, GameLink = 1 };

    struct Player {
        uint8_t memberId = 0; // dans le salon (0 = l'hôte)
        uint16_t linkId = 0;  // pour le core : 0 = l'hôte, puis 1, 2, 3
        std::string name;
    };
    struct Received {
        uint16_t from = 0;
        std::string data;
    };

    // Ce message du salon est-il pour nous ? (Sinon il est pour Netplay.)
    static bool isLinkMessage(const std::string& data);

    // Les consoles portables : chaque joueur avait SA console, reliée aux
    // autres par câble ou sans fil. Pour elles, le salon propose d'office
    // "chacun sa console" (pas besoin du même fichier de jeu).
    struct ConsoleLink {
        const char* coreFile;    // le core qui sait relier plusieurs consoles
        const char* coreName;
        Transport transport;
        int maxPlayers;
        const char* description; // pour le salon
    };
    // nullptr : console de salon (une seule console, plusieurs manettes).
    static const ConsoleLink* consoleLink(std::string_view systemId);

    // --- Hôte : lancer une partie en mode câble ----------------------------
    bool hostStart(Session& session, Transport transport, std::string& error);

    void onAppMessage(Session& session, uint8_t from, const std::string& data);
    void update(Session& session); // à chaque image
    void onMemberLeft(Session& session, const std::string& reason);
    void onSalonClosed(const std::string& reason);

    // --- Chargement : App démarre le jeu de CE joueur, puis rend compte --------
    bool wantsLaunch() const { return m_phase == Phase::Loading && !m_launched; }
    // protocol : version du protocole du core (les joueurs doivent avoir la même).
    void launched(Session& session, bool ok, const std::string& protocol, const std::string& error);
    // Vrai une fois, au moment où la partie commence (App démarre alors le câble).
    bool takeStarted();

    // --- Partie -------------------------------------------------------------------
    // Envoie un paquet du core (to : numéro de joueur, ou 0xFFFF = tous).
    void send(Session& session, uint16_t to, const std::string& data);
    std::vector<Received> takeReceived();
    // GameLink : les adresses de notre relais, et celles du relais de l'ami.
    void sendTunnelAddresses(Session& session, const std::vector<Address>& addresses);
    std::optional<std::vector<Address>> takePeerAddresses();

    void stop(Session& session, const std::string& reason);
    std::vector<std::string> takeMessages();
    bool takeStopped();

    // --- Informations ---------------------------------------------------------------
    bool active() const { return m_phase != Phase::Idle; }
    bool running() const { return m_phase == Phase::Running; }
    bool isHost() const { return m_isHost; }
    Transport transport() const { return m_transport; }
    uint16_t localId() const { return m_localId; }
    const std::vector<Player>& players() const { return m_players; }

private:
    void finish(const std::string& reason);
    void deliver(uint16_t from, uint16_t to, bool more, const std::string& data);
    const Player* byMember(uint8_t memberId) const;
    const Player* byLink(uint16_t linkId) const;

    Phase m_phase = Phase::Idle;
    Transport m_transport = Transport::Netpacket;
    bool m_isHost = false;
    bool m_launched = false;
    bool m_started = false;
    bool m_stopped = false;
    uint16_t m_localId = 0;
    std::vector<Player> m_players;
    std::string m_localProtocol;
    std::map<uint8_t, std::string> m_loaded; // hôte : membre -> protocole de son core

    std::vector<Received> m_received;
    std::map<uint16_t, std::string> m_partial; // paquets découpés, en cours de réception
    std::optional<std::vector<Address>> m_peerAddresses;
    std::vector<std::string> m_messages;
};

} // namespace net
