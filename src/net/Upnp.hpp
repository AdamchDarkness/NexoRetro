#pragma once
// Upnp : demander à la box d'ouvrir un port.
//
// La plupart des box acceptent qu'un programme du réseau local leur demande
// "redirige vers moi les paquets qui arrivent sur le port 51000" (UPnP).
// Grâce à ça, l'ami peut joindre l'hôte directement, sans autre réglage.
//
// Les échanges avec la box prennent une à deux secondes : ils se font dans
// un fil d'exécution (thread) à part, pour ne pas figer l'interface.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace net {

class PortMapping {
public:
    PortMapping() = default;
    ~PortMapping(); // referme le port sur la box
    PortMapping(const PortMapping&) = delete;
    PortMapping& operator=(const PortMapping&) = delete;

    // Lance la demande en arrière-plan.
    void start(uint16_t port);
    bool finished() const { return m_finished; }
    bool succeeded() const { return m_succeeded; }
    uint32_t externalIp() const { return m_externalIp; }
    std::string message() const;

private:
    void run(uint16_t port);
    void remove();

    // [C++] std::thread lance une fonction en parallèle ; std::atomic est
    // une variable qu'on peut lire depuis un fil pendant qu'un autre l'écrit.
    std::thread m_thread;
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_succeeded{false};
    std::atomic<uint32_t> m_externalIp{0};
    mutable std::mutex m_mutex;
    std::string m_message;
    std::string m_controlUrl, m_serviceType, m_port;
};

} // namespace net
