#include "Relay.hpp"

#include "InviteCode.hpp"

#include <algorithm>
#include <cstring>

namespace net {

namespace {

const char* const kStunHosts[] = {"stun.l.google.com", "stun.cloudflare.com"};
constexpr uint16_t kStunPorts[] = {19302, 3478};
constexpr Uint64 kRoomTimeoutMs = 15000;   // hôte silencieux : salon oublié
constexpr Uint64 kPlayerTimeoutMs = 15000; // joueur silencieux : place libérée
constexpr size_t kMaxRooms = 32;
constexpr size_t kMaxPlayersPerRoom = 8;

uint32_t read32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

} // namespace

std::string relay::packet(Op op, uint32_t room, uint8_t slot, const std::string& payload)
{
    std::string p(reinterpret_cast<const char*>(kMagic), 4);
    p += static_cast<char>(op);
    p += static_cast<char>(room >> 24);
    p += static_cast<char>(room >> 16);
    p += static_cast<char>(room >> 8);
    p += static_cast<char>(room);
    p += static_cast<char>(slot);
    p += payload;
    return p;
}

RelayServer::~RelayServer()
{
    stop();
}

bool RelayServer::start()
{
    stop();
    // Un port connu (le réseau local le trouve), sinon n'importe lequel.
    bool opened = false;
    for (int i = 0; i < relay::kPorts && !opened; ++i)
        opened = m_socket.open(static_cast<uint16_t>(relay::kPortBase + i));
    if (!opened && !m_socket.open(0)) {
        m_status = "Impossible d'ouvrir un port réseau";
        return false;
    }
    m_stun.emplace();
    m_stunServers.clear();
    for (size_t i = 0; i < std::size(kStunHosts); ++i)
        if (auto a = resolve(kStunHosts[i], kStunPorts[i]))
            m_stunServers.push_back(*a);
    m_stunAttempts = 0;
    m_stunLastSend = 0;
    m_publicAddress.reset();
    m_upnp = std::make_unique<PortMapping>();
    m_upnp->start(m_socket.localPort());
    m_started = SDL_GetTicks();
    m_status = "Ouverture du relais…";
    SDL_Log("Relais : port %u", m_socket.localPort());
    return true;
}

void RelayServer::stop()
{
    m_socket.close();
    m_upnp.reset(); // referme le port sur la box
    m_stun.reset();
    m_rooms.clear();
    m_code.clear();
    m_bytes = 0;
}

int RelayServer::players() const
{
    int n = 0;
    for (const auto& [id, room] : m_rooms)
        n += static_cast<int>(room.slots.size());
    return n;
}

void RelayServer::update()
{
    if (!m_socket.isOpen())
        return;
    const Uint64 now = SDL_GetTicks();
    uint8_t buffer[2048];
    Address from;
    while (size_t n = m_socket.receive(from, buffer, sizeof(buffer))) {
        if (StunRequest::looksLikeStun(buffer, n)) {
            if (m_stun && !m_publicAddress)
                m_publicAddress = m_stun->parseResponse(buffer, n);
            continue;
        }
        if (n < relay::kHeader || std::memcmp(buffer, relay::kMagic, 4) != 0)
            continue;
        const auto op = static_cast<relay::Op>(buffer[4]);
        const uint32_t room = read32(buffer + 5);
        const uint8_t slot = buffer[9];
        const std::string payload(reinterpret_cast<const char*>(buffer + relay::kHeader), n - relay::kHeader);
        if (op == relay::Op::Register) {
            // L'hôte (re)déclare son salon.
            auto it = m_rooms.find(room);
            if (it == m_rooms.end() && m_rooms.size() >= kMaxRooms)
                continue;
            // Un salon tenu par un autre PC ne se laisse pas prendre (tant que son hôte répond).
            if (it != m_rooms.end() && !(it->second.host == from) && now - it->second.lastHost < kRoomTimeoutMs)
                continue;
            Room& r = m_rooms[room];
            r.host = from;
            r.lastHost = now;
            const std::string ack = relay::packet(relay::Op::Registered, room, 0);
            m_socket.sendTo(from, ack.data(), ack.size());
        } else if (op == relay::Op::Data) {
            auto it = m_rooms.find(room);
            if (it == m_rooms.end()) {
                const std::string nope = relay::packet(relay::Op::NoRoom, room, 0);
                m_socket.sendTo(from, nope.data(), nope.size());
                continue;
            }
            Room& r = it->second;
            if (from == r.host) {
                // De l'hôte vers la place "slot".
                r.lastHost = now;
                if (const auto dest = r.slots.find(slot); dest != r.slots.end()) {
                    const std::string out = relay::packet(relay::Op::Data, room, 0, payload);
                    m_socket.sendTo(dest->second, out.data(), out.size());
                    m_bytes += out.size();
                }
            } else {
                // D'un joueur : sa place (attribuée à son premier paquet), puis vers l'hôte.
                uint8_t place = 0;
                for (const auto& [s, addr] : r.slots)
                    if (addr == from)
                        place = s;
                if (place == 0) {
                    if (r.slots.size() >= kMaxPlayersPerRoom)
                        continue;
                    for (uint8_t s = 1; s < 255 && place == 0; ++s)
                        if (!r.slots.count(s))
                            place = s;
                    r.slots[place] = from;
                    SDL_Log("Relais : un joueur rejoint le salon %08X (place %u)", room, place);
                }
                r.lastSeen[place] = now;
                const std::string out = relay::packet(relay::Op::Data, room, place, payload);
                m_socket.sendTo(r.host, out.data(), out.size());
                m_bytes += out.size();
            }
        }
    }
    // Ménage : salons et joueurs silencieux.
    for (auto it = m_rooms.begin(); it != m_rooms.end();) {
        Room& r = it->second;
        for (auto s = r.slots.begin(); s != r.slots.end();) {
            if (now - r.lastSeen[s->first] > kPlayerTimeoutMs) {
                r.lastSeen.erase(s->first);
                s = r.slots.erase(s);
            } else {
                ++s;
            }
        }
        it = now - r.lastHost > kRoomTimeoutMs ? m_rooms.erase(it) : std::next(it);
    }

    // Préparation : adresse publique (STUN) et port ouvert sur la box (UPnP).
    if (m_stun && !m_publicAddress && m_stunAttempts < 6 && now - m_stunLastSend > 500 && !m_stunServers.empty()) {
        const std::vector<uint8_t> request = m_stun->bytes();
        for (const Address& server : m_stunServers)
            m_socket.sendTo(server, request.data(), request.size());
        m_stunLastSend = now;
        ++m_stunAttempts;
    }
    const bool upnpDone = !m_upnp || m_upnp->finished();
    if (m_code.empty() && ((m_publicAddress || m_stunAttempts >= 6 || m_stunServers.empty()) && upnpDone || now - m_started > 5000)) {
        // Le code : port ouvert par la box, adresse publique, adresses locales,
        // et ce PC lui-même (pour tester seul).
        std::vector<Address> list;
        auto add = [&](Address a) {
            if (a.valid() && std::find(list.begin(), list.end(), a) == list.end() && list.size() < 5)
                list.push_back(a);
        };
        if (m_upnp && m_upnp->succeeded() && m_upnp->externalIp())
            add({m_upnp->externalIp(), m_socket.localPort()});
        if (m_publicAddress)
            add(*m_publicAddress);
        for (uint32_t ip : localAddresses())
            if (isPrivate(ip))
                add({ip, m_socket.localPort()});
        add({0x7F000001, m_socket.localPort()});
        m_code = encodeRelayCode(list);
        const bool reachable = (m_upnp && m_upnp->succeeded()) || m_publicAddress;
        m_status = reachable ? "Relais prêt" : "Relais prêt (réseau local seulement : la box n'a pas ouvert de port)";
        SDL_Log("Relais prêt : %s", m_code.c_str());
    }
}

} // namespace net
