#pragma once
// Relay : jouer en ligne SANS montrer son adresse IP, grâce au relais d'un ami.
//
// Normalement, les PC se parlent en direct : chacun voit l'adresse IP des
// autres. Avec un relais, un ami de confiance fait passer tous les paquets
// par son PC :
//
//     hôte  <--->  relais (l'ami)  <--->  joueurs
//
// Les joueurs ne connaissent que l'adresse du relais, et l'hôte ne voit
// jamais la leur. Les paquets sont chiffrés de bout en bout (Crypto.hpp,
// clé dans le code d'invitation) : le relais les transmet sans pouvoir les
// lire ni les modifier. Ça aide aussi quand deux box refusent de se parler
// en direct (le relais, lui, a un port ouvert).
//
// Format d'un paquet du relais :
//   "NXRL" | opération (1) | salon (4) | place (1) | contenu
// La "place" numérote les joueurs d'un salon (1, 2, 3...) ; 0 = l'hôte.

#include "Socket.hpp"
#include "Stun.hpp"
#include "Upnp.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace net {

namespace relay {
constexpr uint8_t kMagic[4] = {'N', 'X', 'R', 'L'};
constexpr size_t kHeader = 10;
enum class Op : uint8_t {
    Register = 1, // hôte -> relais : "je tiens ce salon" (répété : il garde le salon en vie)
    Registered,   // relais -> hôte
    Data,         // un paquet du salon, chiffré, à transmettre
    NoRoom,       // relais -> joueur : ce salon n'existe pas (ou plus)
};
constexpr uint16_t kPortBase = 47820; // ports du relais (comme ceux des salons, mais à part)
constexpr int kPorts = 5;
// Les adresses "virtuelles" des autres, vues à travers le relais : 0.0.0.1
// et le numéro de place (l'hôte a la place 0, vue des joueurs : port 0xFFFF).
constexpr uint32_t kVirtualIp = 1;
constexpr uint16_t kHostPort = 0xFFFF;

std::string packet(Op op, uint32_t room, uint8_t slot, const std::string& payload = {});
} // namespace relay

// Le relais lui-même, sur le PC de l'ami qui le propose (écran Amis).
class RelayServer {
public:
    RelayServer() = default;
    ~RelayServer();

    bool start();
    void stop();
    bool running() const { return m_socket.isOpen(); }
    // À appeler à chaque image : transmet les paquets arrivés.
    void update();

    // Le code à donner à l'hôte (prêt quand ready() : adresse publique trouvée).
    bool ready() const { return !m_code.empty(); }
    const std::string& code() const { return m_code; }
    int rooms() const { return static_cast<int>(m_rooms.size()); }
    int players() const;
    uint64_t bytesRelayed() const { return m_bytes; }
    const std::string& status() const { return m_status; }

private:
    struct Room {
        Address host;
        Uint64 lastHost = 0;
        std::map<uint8_t, Address> slots; // place -> adresse du joueur
        std::map<uint8_t, Uint64> lastSeen;
    };

    UdpSocket m_socket;
    std::optional<StunRequest> m_stun;
    std::vector<Address> m_stunServers;
    Uint64 m_stunLastSend = 0;
    int m_stunAttempts = 0;
    std::optional<Address> m_publicAddress;
    std::unique_ptr<PortMapping> m_upnp;
    Uint64 m_started = 0;
    std::string m_code;
    std::string m_status;
    std::map<uint32_t, Room> m_rooms;
    uint64_t m_bytes = 0;
};

} // namespace net
