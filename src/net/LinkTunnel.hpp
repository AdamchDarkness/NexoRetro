#pragma once
// LinkTunnel : le relais du câble link Game Boy (core Gambatte).
//
// Gambatte relie deux émulateurs par une connexion TCP directe : l'un est
// "serveur" (il attend), l'autre "client" (il se connecte à une adresse IP).
// Entre deux amis derrière leurs box, cette connexion directe échouerait.
// Alors sur chaque PC, ce relais se fait passer pour "l'autre Gambatte" :
//
//   Gambatte (hôte) <-TCP local-> relais <==== Internet (UDP) ====> relais <-TCP local-> Gambatte (ami)
//
// Entre les deux relais, on passe par UDP (comme le salon, pour traverser
// les box) en rendant le flux fiable : chaque paquet est numéroté, l'autre
// accuse réception, et ce qui n'est pas confirmé est renvoyé.
//
// Gambatte attend la réponse à chaque octet envoyé, en bloquant son image :
// le relais tourne donc dans son propre thread, indépendant de l'émulation.

#include "Socket.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace net {

class LinkTunnel {
public:
    // ConnectToCore : chez l'hôte, Gambatte est serveur : le relais s'y connecte.
    // ListenForCore : chez l'ami, Gambatte est client : le relais l'attend.
    enum class Role { ConnectToCore, ListenForCore };

    LinkTunnel(Role role, uint16_t corePort);
    ~LinkTunnel();
    LinkTunnel(const LinkTunnel&) = delete;
    LinkTunnel& operator=(const LinkTunnel&) = delete;

    // Les adresses où l'ami peut joindre notre relais (réseau local + adresse
    // publique), une fois trouvées (une seconde environ).
    std::optional<std::vector<Address>> addresses() const;
    // Les adresses du relais de l'ami (reçues par le salon).
    void setPeer(const std::vector<Address>& candidates);

    bool listening() const { return m_listening; } // ListenForCore : prêt à accueillir Gambatte
    std::string status() const;                     // pour l'écran de jeu

private:
    struct Pending {
        uint32_t seq;
        std::string data;
        uint64_t sentAt;
    };

    void run();
    void findPublicAddress();
    void handleUdp(const Address& from, const uint8_t* data, size_t size);
    void sendToPeer(char type, const std::string& payload = {});
    void writeToCore(const std::string& data);
    void closeCore();

    const Role m_role;
    const uint16_t m_corePort;
    UdpSocket m_udp;
    uintptr_t m_listener = ~uintptr_t(0); // ListenForCore : la socket TCP d'écoute
    uintptr_t m_core = ~uintptr_t(0);     // la connexion TCP avec Gambatte

    mutable std::mutex m_mutex; // protège ce qui est partagé avec le thread principal
    std::optional<std::vector<Address>> m_addresses;
    std::vector<Address> m_candidates;
    std::optional<Address> m_peer;
    std::string m_status = "Câble : préparation…";

    // Flux fiable (thread du relais uniquement).
    uint32_t m_nextSeq = 0;       // numéro du prochain paquet à envoyer
    uint32_t m_expected = 0;      // numéro du prochain paquet attendu
    std::vector<Pending> m_unacked;
    std::vector<std::pair<uint32_t, std::string>> m_early; // arrivés en avance
    std::string m_toCore;         // reçu avant que Gambatte soit connecté
    bool m_peerCoreReady = false; // le Gambatte de l'ami est branché
    uint64_t m_lastHeard = 0;
    uint64_t m_lastHello = 0;
    uint64_t m_lastKeepAlive = 0;
    uint64_t m_lastConnectTry = 0;

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

} // namespace net
