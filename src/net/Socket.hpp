#pragma once
// Socket : envoyer et recevoir des paquets UDP.
//
// UDP envoie des paquets indépendants, sans connexion ni garantie
// d'arrivée. C'est ce qu'utilisent les jeux en ligne : un paquet en retard
// ne sert plus à rien, mieux vaut le suivant. Quand une information DOIT
// arriver (un message de chat), c'est Session qui la renvoie jusqu'à
// recevoir un accusé de réception.
//
// Les sockets de Windows (Winsock) ne sont utilisés que dans Socket.cpp :
// le reste du programme ne voit que cette petite classe.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace net {

// Une adresse IPv4 + un port, ex. 82.64.12.7:51000.
struct Address {
    uint32_t ip = 0;   // dans l'ordre "humain" : 0x7F000001 = 127.0.0.1
    uint16_t port = 0;

    std::string toString() const;
    bool valid() const { return ip != 0 && port != 0; }
    bool operator==(const Address&) const = default; // [C++20] comparaison générée
};

// Initialise Winsock pour tout le programme (une fois, au démarrage).
class NetworkSystem {
public:
    NetworkSystem();
    ~NetworkSystem();
    NetworkSystem(const NetworkSystem&) = delete;
    NetworkSystem& operator=(const NetworkSystem&) = delete;
};

// "stun.l.google.com" -> adresse IPv4. Bloquant (quelques ms en général).
std::optional<Address> resolve(const std::string& host, uint16_t port);
// Les adresses IPv4 de ce PC sur ses réseaux locaux (box, Radmin, Hamachi...).
std::vector<uint32_t> localAddresses();
// Les adresses de "diffusion" de ces réseaux (192.168.1.255...) : un paquet
// envoyé là arrive à tous les PC du réseau. 255.255.255.255 compris.
std::vector<uint32_t> broadcastAddresses();
bool isPrivate(uint32_t ip);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port = 0 : Windows choisit un port libre.
    bool open(uint16_t port = 0);
    void close();
    bool isOpen() const;
    uint16_t localPort() const { return m_port; }
    // Le SOCKET Windows (pour attendre plusieurs sockets à la fois avec select).
    uintptr_t handle() const { return m_handle; }

    bool sendTo(const Address& to, const void* data, size_t size);
    // Non bloquant : renvoie 0 s'il n'y a rien à lire.
    size_t receive(Address& from, void* buffer, size_t capacity);

private:
    // Le "SOCKET" de Windows est un entier ; on évite d'inclure winsock2.h ici.
    uintptr_t m_handle = ~uintptr_t(0);
    uint16_t m_port = 0;
};

} // namespace net
