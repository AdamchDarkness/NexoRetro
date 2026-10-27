#include "Socket.hpp"

#include <algorithm>

#include "SocketCompat.hpp"
#ifdef _WIN32
#include <iphlpapi.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET // pas toujours défini selon la version du SDK Windows
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <ifaddrs.h> // getifaddrs : la liste des cartes réseau (Android)
#include <net/if.h>
#endif

#include <SDL3/SDL.h>

#include <cstdio>

namespace net {

namespace {
constexpr uintptr_t kInvalid = ~uintptr_t(0);

sockaddr_in toSockaddr(const Address& a)
{
    sockaddr_in s{};
    s.sin_family = AF_INET;
    // htonl/htons : le réseau range les octets "gros-boutiste", le PC
    // "petit-boutiste". Ces fonctions font la conversion.
    s.sin_addr.s_addr = htonl(a.ip);
    s.sin_port = htons(a.port);
    return s;
}
} // namespace

std::string Address::toString() const
{
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                  port);
    return text;
}

NetworkSystem::NetworkSystem()
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        SDL_Log("Réseau : WSAStartup a échoué");
#endif
}

NetworkSystem::~NetworkSystem()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

std::optional<Address> resolve(const std::string& host, uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
        return std::nullopt;
    const auto* in = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    Address a{ntohl(in->sin_addr.s_addr), port};
    freeaddrinfo(result);
    return a;
}

bool isPrivate(uint32_t ip)
{
    const uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           a == 25 || a == 26 ||                 // Hamachi (25.x), Radmin VPN (26.x)
           (a == 100 && b >= 64 && b <= 127);    // Tailscale, réseaux d'opérateurs
}

namespace {

// Les adresses de ce PC et la taille de leur réseau (24 pour 192.168.1.x).
std::vector<std::pair<uint32_t, int>> adapterAddresses()
{
#ifndef _WIN32
    // getifaddrs : la version POSIX (Android) de GetAdaptersAddresses.
    std::vector<std::pair<uint32_t, int>> found;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
        return found;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK))
            continue;
        const uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        if ((ip >> 16) == 0xA9FE) // 169.254.x.x : adresse "sans réseau"
            continue;
        int prefix = 0;
        if (a->ifa_netmask) {
            const uint32_t mask = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
            prefix = __builtin_popcount(mask);
        }
        found.push_back({ip, prefix});
    }
    freeifaddrs(list);
    return found;
#else
    // GetAdaptersAddresses liste toutes les cartes réseau, y compris les
    // cartes virtuelles des VPN comme Radmin ou Hamachi.
    std::vector<std::pair<uint32_t, int>> result;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                             adapters, &size) == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, adapters, &size) != NO_ERROR)
            return result;
    }
    for (auto* a = adapters; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const auto* in = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            const uint32_t ip = ntohl(in->sin_addr.s_addr);
            if ((ip >> 16) == 0xA9FE) // 169.254.x.x : adresse "sans réseau"
                continue;
            result.push_back({ip, u->OnLinkPrefixLength});
        }
    }
    return result;
#endif
}

} // namespace

std::vector<uint32_t> localAddresses()
{
    std::vector<uint32_t> result;
    for (const auto& [ip, prefix] : adapterAddresses())
        result.push_back(ip);
    return result;
}

std::vector<uint32_t> broadcastAddresses()
{
    // 192.168.1.20/24 -> 192.168.1.255 : tous les PC de ce réseau à la fois.
    std::vector<uint32_t> result = {0xFFFFFFFFu};
    for (const auto& [ip, prefix] : adapterAddresses()) {
        if (!isPrivate(ip) || prefix <= 0 || prefix >= 31)
            continue;
        const uint32_t broadcast = ip | (0xFFFFFFFFu >> prefix);
        if (std::find(result.begin(), result.end(), broadcast) == result.end())
            result.push_back(broadcast);
    }
    return result;
}

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::open(uint16_t port)
{
    close();
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return false;
    }
    // Permet d'envoyer à une adresse de diffusion (tout le réseau local).
    BOOL broadcast = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    // Mode non bloquant : recvfrom rend la main tout de suite s'il n'y a rien.
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    // Windows signale par une "erreur" qu'un paquet UDP précédent n'a pas
    // trouvé de destinataire ; on désactive ce comportement gênant.
#ifdef _WIN32
    BOOL reportReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0, &bytes, nullptr, nullptr);
#endif

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    m_port = ntohs(bound.sin_port);
    m_handle = static_cast<uintptr_t>(s);
    return true;
}

void UdpSocket::close()
{
    if (m_handle != kInvalid)
        closesocket(static_cast<SOCKET>(m_handle));
    m_handle = kInvalid;
    m_port = 0;
}

bool UdpSocket::isOpen() const
{
    return m_handle != kInvalid;
}

bool UdpSocket::sendTo(const Address& to, const void* data, size_t size)
{
    if (!isOpen())
        return false;
    const sockaddr_in addr = toSockaddr(to);
    return sendto(static_cast<SOCKET>(m_handle), static_cast<const char*>(data), static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == static_cast<int>(size);
}

size_t UdpSocket::receive(Address& from, void* buffer, size_t capacity)
{
    if (!isOpen())
        return 0;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    const int n = recvfrom(static_cast<SOCKET>(m_handle), static_cast<char*>(buffer), static_cast<int>(capacity), 0,
                           reinterpret_cast<sockaddr*>(&addr), &len);
    if (n <= 0)
        return 0;
    from = {ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)};
    return static_cast<size_t>(n);
}

} // namespace net
