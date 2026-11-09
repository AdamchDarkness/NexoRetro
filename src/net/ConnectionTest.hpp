#pragma once
// ConnectionTest : "Tester ma connexion" (écran Amis).
//
// Quand deux amis n'arrivent pas à se joindre, il est difficile de savoir
// pourquoi. Ce test regarde, en quelques secondes, ce qui peut bloquer :
//
//   1. Internet répond-il ? On pose la question "comment me vois-tu ?" à
//      plusieurs serveurs STUN publics (voir Stun.hpp).
//   2. La box garde-t-elle le même port pour tout le monde ? Si chaque
//      serveur voit un port différent, la box change de port selon la
//      destination (NAT "symétrique") : l'ami ne peut pas deviner le bon,
//      et la connexion directe échoue souvent.
//   3. La box accepte-t-elle d'ouvrir un port (UPnP) ?
//   4. Le pare-feu de Windows laisse-t-il entrer les paquets de Nexo Retro ?
//
// Comme Session, tout avance dans update(), appelée à chaque image ; seules
// les questions à la box (UPnP) et au pare-feu se font dans un fil à part.

#include "Socket.hpp"
#include "Stun.hpp"
#include "Upnp.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace net {

class ConnectionTest {
public:
    // Ce que pense le pare-feu de Windows des paquets entrants.
    enum class Firewall { Checking, Allowed, Blocked, Unknown };
    // Comment la box choisit le port public.
    enum class Mapping {
        NoAnswer,  // aucun serveur n'a répondu : pas d'Internet, ou UDP bloqué
        Stable,    // le même port pour tous : idéal
        Nearby,    // des ports voisins : le code de réponse essaie ces voisins
        Random,    // des ports très différents : la connexion directe échouera souvent
    };
    // Le bilan, du meilleur au pire.
    enum class Verdict { Ready, NeedsAnswer, Difficult, Blocked, NoInternet };

    ConnectionTest() = default;
    ~ConnectionTest();
    ConnectionTest(const ConnectionTest&) = delete;
    ConnectionTest& operator=(const ConnectionTest&) = delete;

    void start();
    void update();
    bool running() const { return m_active && !m_done; }
    bool done() const { return m_done; }

    // Résultats (valables quand done()).
    Mapping mapping() const { return m_mapping; }
    bool upnp() const { return m_upnp && m_upnp->succeeded(); }
    Firewall firewall() const { return m_firewall; }
    Verdict verdict() const;
    std::optional<Address> publicAddress() const;
    uint16_t localPort() const { return m_port; }
    int answers() const;            // nombre de serveurs STUN qui ont répondu
    int portSpread() const;         // écart entre le plus petit et le plus grand port vu
    // Un rapport en texte brut, à copier pour un ami (ou pour un message d'aide).
    std::string report() const;

private:
    struct Server {
        std::string name;
        Address address;
        std::optional<Address> seen; // notre adresse publique, vue par ce serveur
    };
    void finish();
    void checkFirewall(uint16_t port);

    UdpSocket m_socket;
    std::optional<StunRequest> m_stun;
    std::vector<Server> m_servers;
    std::unique_ptr<PortMapping> m_upnp;
    uint16_t m_port = 0; // (gardé après la fermeture du port)
    Uint64 m_started = 0;
    Uint64 m_lastSend = 0;
    int m_attempts = 0;
    bool m_active = false; // start() appelée
    bool m_done = false;
    Mapping m_mapping = Mapping::NoAnswer;

    std::thread m_firewallThread;
    std::atomic<Firewall> m_firewall{Firewall::Unknown};
};

const char* verdictTitle(ConnectionTest::Verdict verdict);  // "Votre connexion est prête"...
const char* verdictAdvice(ConnectionTest::Verdict verdict); // la marche à suivre

} // namespace net
