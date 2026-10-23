#include "LinkTunnel.hpp"

#include "Bytes.hpp"
#include "Stun.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace net {

namespace {

constexpr uintptr_t kNoSocket = ~uintptr_t(0);
// Nos paquets UDP commencent par ces deux lettres (rien à voir avec STUN).
constexpr char kMagic0 = 'E', kMagic1 = 'L';
// Types : H bonjour, h réponse au bonjour, D données, A accusé de réception,
// K "toujours là" (avec l'état du Gambatte local : branché ou non).
constexpr uint64_t kHelloEveryMs = 100;
constexpr uint64_t kResendAfterMs = 100;
constexpr uint64_t kKeepAliveMs = 500;
constexpr uint64_t kLostAfterMs = 15000;

const char* const kStunHosts[] = {"stun.l.google.com", "stun.cloudflare.com"};
constexpr uint16_t kStunPorts[] = {19302, 3478};

SOCKET sock(uintptr_t s)
{
    return static_cast<SOCKET>(s);
}

void setNoDelay(uintptr_t s)
{
    // Pas d'attente pour regrouper les petits envois : Gambatte envoie 2
    // octets et attend la réponse, chaque milliseconde compte.
    BOOL yes = TRUE;
    setsockopt(sock(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
}

sockaddr_in loopback(uint16_t port)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

} // namespace

LinkTunnel::LinkTunnel(Role role, uint16_t corePort) : m_role(role), m_corePort(corePort)
{
    m_udp.open(0);
    // L'écoute doit exister AVANT que Gambatte démarre : il tente de se
    // connecter dès le chargement du jeu (puis toutes les 5 secondes).
    if (m_role == Role::ListenForCore) {
        const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a = loopback(corePort);
        if (s != INVALID_SOCKET && bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 && listen(s, 1) == 0) {
            m_listener = s;
            m_listening = true;
        } else {
            if (s != INVALID_SOCKET)
                closesocket(s);
            m_status = "Câble : port " + std::to_string(corePort) + " déjà utilisé";
        }
    }
    m_thread = std::thread([this] { run(); });
}

LinkTunnel::~LinkTunnel()
{
    m_stop = true;
    m_thread.join();
    closeCore();
    if (m_listener != kNoSocket)
        closesocket(sock(m_listener));
}

std::optional<std::vector<Address>> LinkTunnel::addresses() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_addresses;
}

void LinkTunnel::setPeer(const std::vector<Address>& candidates)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_candidates = candidates;
}

std::string LinkTunnel::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

// --- Le thread du relais ------------------------------------------------------------

void LinkTunnel::findPublicAddress()
{
    // Toutes nos adresses locales (même réseau, même PC), puis l'adresse
    // publique vue d'Internet, demandée à un serveur STUN.
    std::vector<Address> list;
    const uint16_t port = m_udp.localPort();
    list.push_back({0x7F000001, port});
    for (uint32_t ip : localAddresses())
        list.push_back({ip, port});

    std::vector<Address> servers;
    for (size_t i = 0; i < std::size(kStunHosts); ++i)
        if (auto a = resolve(kStunHosts[i], kStunPorts[i]))
            servers.push_back(*a);
    StunRequest request;
    const std::vector<uint8_t> bytes = request.bytes();
    std::optional<Address> mine;
    for (int attempt = 0; attempt < 5 && !mine && !servers.empty() && !m_stop; ++attempt) {
        for (const Address& s : servers)
            m_udp.sendTo(s, bytes.data(), bytes.size());
        const uint64_t until = SDL_GetTicks() + 300;
        while (!mine && SDL_GetTicks() < until && !m_stop) {
            uint8_t buffer[1500];
            Address from;
            if (const size_t n = m_udp.receive(from, buffer, sizeof(buffer)))
                mine = request.parseResponse(buffer, n);
            else
                SDL_Delay(5);
        }
    }
    if (mine && std::find(list.begin(), list.end(), *mine) == list.end())
        list.push_back(*mine);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_addresses = std::move(list);
    m_status = "Câble : recherche de l'ami…";
}

void LinkTunnel::run()
{
    findPublicAddress();
    while (!m_stop) {
        // Attendre (5 ms au plus) qu'une des sockets ait quelque chose à lire.
        fd_set read;
        FD_ZERO(&read);
        FD_SET(sock(m_udp.handle()), &read);
        if (m_listener != kNoSocket && m_core == kNoSocket)
            FD_SET(sock(m_listener), &read);
        if (m_core != kNoSocket)
            FD_SET(sock(m_core), &read);
        timeval timeout{0, 5000};
        select(0, &read, nullptr, nullptr, &timeout);
        const uint64_t now = SDL_GetTicks();

        // 1. Les paquets de l'ami.
        uint8_t buffer[2048];
        Address from;
        while (const size_t n = m_udp.receive(from, buffer, sizeof(buffer)))
            handleUdp(from, buffer, n);

        // 2. Gambatte (chez l'ami) se connecte à notre écoute.
        if (m_listener != kNoSocket && m_core == kNoSocket && FD_ISSET(sock(m_listener), &read)) {
            const SOCKET s = accept(sock(m_listener), nullptr, nullptr);
            if (s != INVALID_SOCKET) {
                m_core = s;
                setNoDelay(m_core);
                SDL_Log("Câble : le jeu est branché au relais");
                m_lastKeepAlive = 0; // prévenir l'autre tout de suite
            }
        }
        // 3. Chez l'hôte : se connecter à Gambatte (serveur), une fois que
        //    celui de l'ami est branché (sinon le jeu attendrait dans le vide).
        if (m_role == Role::ConnectToCore && m_core == kNoSocket && m_peerCoreReady && now - m_lastConnectTry > 500) {
            m_lastConnectTry = now;
            const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in a = loopback(m_corePort);
            if (s != INVALID_SOCKET && connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                m_core = s;
                setNoDelay(m_core);
                SDL_Log("Câble : relais branché au jeu");
                m_lastKeepAlive = 0;
            } else if (s != INVALID_SOCKET) {
                closesocket(s);
            }
        }
        // Ce qui est arrivé avant le branchement.
        if (m_core != kNoSocket && !m_toCore.empty()) {
            const std::string data = std::move(m_toCore);
            m_toCore.clear();
            writeToCore(data);
        }

        // 4. Ce que Gambatte envoie : vers l'ami, numéroté.
        if (m_core != kNoSocket && FD_ISSET(sock(m_core), &read)) {
            char data[1024];
            const int n = recv(sock(m_core), data, sizeof(data), 0);
            if (n <= 0) {
                SDL_Log("Câble : le jeu s'est débranché");
                closeCore();
                m_lastKeepAlive = 0;
            } else {
                Pending p{m_nextSeq++, std::string(data, static_cast<size_t>(n)), now};
                sendToPeer('D', ByteWriter().u32(p.seq).raw(p.data).data());
                m_unacked.push_back(std::move(p));
            }
        }

        // 5. Trouver l'ami : "bonjour" à toutes ses adresses, jusqu'à réponse.
        std::optional<Address> peer;
        std::vector<Address> candidates;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            peer = m_peer;
            candidates = m_candidates;
        }
        if (!peer && !candidates.empty() && now - m_lastHello > kHelloEveryMs) {
            m_lastHello = now;
            const char hello[] = {kMagic0, kMagic1, 'H'};
            for (const Address& c : candidates)
                m_udp.sendTo(c, hello, sizeof(hello));
        }
        if (!peer)
            continue;

        // 6. Renvoyer ce qui n'a pas été confirmé, et donner des nouvelles.
        for (Pending& p : m_unacked) {
            if (now - p.sentAt > kResendAfterMs) {
                p.sentAt = now;
                sendToPeer('D', ByteWriter().u32(p.seq).raw(p.data).data());
            }
        }
        if (now - m_lastKeepAlive > kKeepAliveMs) {
            m_lastKeepAlive = now;
            sendToPeer('K', ByteWriter().u8(m_core != kNoSocket ? 1 : 0).data());
        }
        const bool lost = now - m_lastHeard > kLostAfterMs;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (lost)
            m_status = "Câble : liaison perdue avec l'ami";
        else if (m_core != kNoSocket && m_peerCoreReady)
            m_status = "Câble : branché";
        else
            m_status = "Câble : en attente du jeu de l'ami…";
    }
}

void LinkTunnel::handleUdp(const Address& from, const uint8_t* data, size_t size)
{
    if (size < 3 || data[0] != kMagic0 || data[1] != kMagic1)
        return; // (une réponse STUN tardive, ou autre chose)
    const char type = static_cast<char>(data[2]);
    {
        // Le premier qui répond devient "l'ami" (on garde cette adresse).
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_peer && (type == 'H' || type == 'h')) {
            m_peer = from;
            SDL_Log("Câble : ami joint à l'adresse %s", from.toString().c_str());
        }
        if (!m_peer || !(*m_peer == from))
            return;
    }
    m_lastHeard = SDL_GetTicks();
    ByteReader r(std::string_view(reinterpret_cast<const char*>(data + 3), size - 3));
    switch (type) {
    case 'H': {
        const char reply[] = {kMagic0, kMagic1, 'h'};
        m_udp.sendTo(from, reply, sizeof(reply));
        break;
    }
    case 'K': {
        const bool ready = r.u8() != 0;
        // Le Gambatte de l'ami s'est débranché : on débranche aussi le nôtre
        // (chez l'hôte), il se rebranchera proprement quand l'ami reviendra.
        if (m_peerCoreReady && !ready && m_role == Role::ConnectToCore)
            closeCore();
        m_peerCoreReady = ready;
        break;
    }
    case 'A': {
        const uint32_t next = r.u32();
        std::erase_if(m_unacked, [&](const Pending& p) { return p.seq < next; });
        break;
    }
    case 'D': {
        const uint32_t seq = r.u32();
        const std::string payload = r.rest();
        if (seq == m_expected) {
            writeToCore(payload);
            ++m_expected;
            // Ceux arrivés en avance et qui sont maintenant à leur tour.
            for (bool found = true; found;) {
                found = false;
                for (auto it = m_early.begin(); it != m_early.end(); ++it) {
                    if (it->first == m_expected) {
                        writeToCore(it->second);
                        ++m_expected;
                        m_early.erase(it);
                        found = true;
                        break;
                    }
                }
            }
        } else if (seq > m_expected && m_early.size() < 256) {
            m_early.emplace_back(seq, payload);
        }
        sendToPeer('A', ByteWriter().u32(m_expected).data());
        break;
    }
    default:
        break;
    }
}

void LinkTunnel::sendToPeer(char type, const std::string& payload)
{
    std::optional<Address> peer;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        peer = m_peer;
    }
    if (!peer)
        return;
    std::string packet{kMagic0, kMagic1, type};
    packet += payload;
    m_udp.sendTo(*peer, packet.data(), packet.size());
}

void LinkTunnel::writeToCore(const std::string& data)
{
    if (m_core == kNoSocket) {
        if (m_toCore.size() < 65536)
            m_toCore += data;
        return;
    }
    if (send(sock(m_core), data.data(), static_cast<int>(data.size()), 0) <= 0) {
        closeCore();
        m_toCore += data;
    }
}

void LinkTunnel::closeCore()
{
    if (m_core != kNoSocket) {
        closesocket(sock(m_core));
        m_core = kNoSocket;
    }
}

} // namespace net
