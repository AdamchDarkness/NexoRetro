#include "LinkPlay.hpp"

#include "Bytes.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace net {

namespace {

// Types de messages (minuscules : les majuscules sont au netplay).
constexpr char kStart = 'k';   // hôte -> tous : la liste des joueurs
constexpr char kLoaded = 'l';  // membre -> hôte : mon jeu est démarré (ou pas)
constexpr char kGo = 'g';      // hôte -> tous : c'est parti
constexpr char kPacket = 'p';  // un paquet du câble (morceau)
constexpr char kTunnel = 't';  // GameLink : les adresses de mon relais
constexpr char kStop = 'x';    // fin de la partie

constexpr uint16_t kBroadcast = 0xFFFF;
// Un message du salon doit tenir dans un paquet réseau : les paquets plus
// gros du core sont coupés en morceaux, recollés à l'arrivée.
constexpr size_t kChunk = 900;

} // namespace

bool LinkPlay::isLinkMessage(const std::string& data)
{
    return !data.empty() && std::strchr("klgptx", data[0]) != nullptr;
}

const LinkPlay::ConsoleLink* LinkPlay::consoleLink(std::string_view systemId)
{
    static const ConsoleLink gb{"gambatte_libretro.dll", "Gambatte", Transport::GameLink, 2,
                                "Chacun joue SON jeu avec SA sauvegarde, reliés par le câble Game Boy "
                                "(échanges Pokémon...). 2 joueurs."};
    // gpSP n'émule pas un câble "universel" : il imite les échanges de jeux
    // précis. Pour les autres (Mario Kart...), le câble reste débranché.
    static const ConsoleLink gba{"gpsp_libretro.dll", "gpSP", Transport::Netpacket, 4,
                                 "Chacun joue SON jeu avec SA sauvegarde. Le câble GBA ne marche qu'avec : Pokémon "
                                 "Rubis, Saphir, Émeraude, Rouge Feu, Vert Feuille (et leurs hacks) et Advance Wars "
                                 "1 et 2. Pour les autres jeux (Mario Kart...), le core gpSP ne sait pas émuler le "
                                 "câble."};
    static const ConsoleLink nds{"melondsds_libretro.dll", "melonDS DS", Transport::Netpacket, 4,
                                 "Chacun joue SON jeu avec SA sauvegarde, reliés en sans-fil DS (jusqu'à 4). Le "
                                 "sans-fil DS exige un ping très faible : il marche sur un même réseau, rarement par "
                                 "Internet."};
    if (systemId == "gb" || systemId == "gbc") // même câble, même émulateur
        return &gb;
    if (systemId == "gba")
        return &gba;
    if (systemId == "nds")
        return &nds;
    return nullptr;
}

// --- Lancement -----------------------------------------------------------------

bool LinkPlay::hostStart(Session& session, Transport transport, std::string& error)
{
    const auto& members = session.members();
    if (!session.isHost()) {
        error = "C'est l'hôte qui lance la partie";
        return false;
    }
    if (members.size() < 2) {
        error = "Il faut au moins un ami dans le salon";
        return false;
    }
    // Le câble Game Boy relie deux consoles ; la GBA en accepte quatre.
    const size_t maxPlayers = transport == Transport::GameLink ? 2 : 4;
    if (members.size() > maxPlayers) {
        error = std::to_string(maxPlayers) + " joueurs au plus avec ce câble";
        return false;
    }
    for (const Member& m : members) {
        if (m.status != GameStatus::Ready) {
            error = m.name + " n'a pas encore choisi son jeu";
            return false;
        }
    }

    m_players.clear();
    for (const Member& m : members)
        m_players.push_back({m.id, static_cast<uint16_t>(m_players.size()), m.name});
    m_isHost = true;
    m_transport = transport;
    m_localId = 0;
    m_loaded.clear();
    m_launched = false;
    m_started = false;
    m_stopped = false;
    m_peerAddresses.reset();
    m_phase = Phase::Loading;

    ByteWriter w;
    w.u8(kStart).u8(static_cast<uint8_t>(transport)).u8(static_cast<uint8_t>(m_players.size()));
    for (const Player& p : m_players)
        w.u8(p.memberId).u16(p.linkId).str(p.name);
    session.sendApp(-1, w.data());
    m_messages.push_back("Lancement de la partie avec câble link");
    return true;
}

void LinkPlay::launched(Session& session, bool ok, const std::string& protocol, const std::string& error)
{
    m_launched = true;
    if (!ok) {
        if (m_isHost)
            stop(session, "Impossible de lancer le jeu chez l'hôte : " + error);
        else
            session.sendApp(-1, ByteWriter().u8(kLoaded).u8(0).str(error).data());
        return;
    }
    m_localProtocol = protocol;
    if (!m_isHost)
        session.sendApp(-1, ByteWriter().u8(kLoaded).u8(1).str(protocol).data());
}

bool LinkPlay::takeStarted()
{
    const bool started = m_started;
    m_started = false;
    return started;
}

// --- Messages -------------------------------------------------------------------

void LinkPlay::onAppMessage(Session& session, uint8_t from, const std::string& data)
{
    ByteReader r(data);
    const char type = static_cast<char>(r.u8());
    switch (type) {
    case kStart: {
        if (session.isHost())
            break;
        m_isHost = false;
        m_transport = static_cast<Transport>(r.u8());
        const size_t count = r.u8();
        m_players.clear();
        for (size_t i = 0; i < count; ++i) {
            Player p;
            p.memberId = r.u8();
            p.linkId = r.u16();
            p.name = r.str();
            m_players.push_back(p);
        }
        const Player* me = byMember(session.selfId());
        if (!r.ok() || !me)
            break;
        m_localId = me->linkId;
        m_launched = false;
        m_started = false;
        m_stopped = false;
        m_peerAddresses.reset();
        m_phase = Phase::Loading;
        m_messages.push_back("L'hôte lance la partie avec câble link");
        break;
    }
    case kLoaded: {
        if (!m_isHost || m_phase != Phase::Loading)
            break;
        const bool ok = r.u8() != 0;
        const std::string text = r.str();
        const Player* p = byMember(from);
        if (!p)
            break;
        if (!ok) {
            stop(session, "Impossible de lancer le jeu chez " + p->name + " : " + text);
            break;
        }
        m_loaded[from] = text;
        break;
    }
    case kGo:
        if (!m_isHost && m_phase == Phase::Loading) {
            m_phase = Phase::Running;
            m_started = true;
        }
        break;
    case kPacket: {
        const uint16_t src = r.u16();
        const uint16_t dst = r.u16();
        const bool more = r.u8() != 0;
        const std::string payload = r.rest();
        if (!r.ok() || m_phase != Phase::Running)
            break;
        // L'hôte relaie aux autres membres ce qui ne lui est pas (seulement) destiné.
        if (m_isHost) {
            for (const Player& p : m_players) {
                if (p.linkId == 0 || p.linkId == src)
                    continue;
                if (dst == kBroadcast || dst == p.linkId)
                    session.sendApp(p.memberId, data);
            }
        }
        if (dst == kBroadcast || dst == m_localId)
            deliver(src, dst, more, payload);
        break;
    }
    case kTunnel: {
        std::vector<Address> list;
        const size_t n = r.u8();
        for (size_t i = 0; i < n; ++i) {
            Address a;
            a.ip = r.u32();
            a.port = r.u16();
            list.push_back(a);
        }
        if (r.ok())
            m_peerAddresses = std::move(list);
        break;
    }
    case kStop: {
        const std::string reason = r.str();
        if (active())
            finish(reason.empty() ? "La partie est terminée" : reason);
        break;
    }
    default:
        break;
    }
}

void LinkPlay::deliver(uint16_t from, uint16_t, bool more, const std::string& data)
{
    std::string& partial = m_partial[from];
    partial += data;
    if (more)
        return;
    m_received.push_back({from, std::move(partial)});
    m_partial.erase(from);
}

// Hôte : tout le monde a-t-il démarré son jeu ? Alors, départ.
void LinkPlay::update(Session& session)
{
    if (!m_isHost || m_phase != Phase::Loading || !m_launched || m_loaded.size() + 1 < m_players.size())
        return;
    // Le câble ne marche qu'entre deux cores qui parlent le même protocole.
    for (const auto& [id, protocol] : m_loaded) {
        if (protocol != m_localProtocol) {
            const Player* p = byMember(id);
            stop(session, "Émulateurs différents : " + (p ? p->name : std::string("un joueur")) + " utilise " +
                              protocol + ", l'hôte " + m_localProtocol);
            return;
        }
    }
    session.sendApp(-1, ByteWriter().u8(kGo).data());
    m_phase = Phase::Running;
    m_started = true;
}

void LinkPlay::onMemberLeft(Session& session, const std::string& reason)
{
    if (active())
        stop(session, reason);
}

void LinkPlay::onSalonClosed(const std::string& reason)
{
    if (active())
        finish(reason);
}

// --- Partie -------------------------------------------------------------------------

void LinkPlay::send(Session& session, uint16_t to, const std::string& data)
{
    if (m_phase != Phase::Running)
        return;
    // Coupé en morceaux si besoin ; "more" dit si d'autres morceaux suivent.
    for (size_t pos = 0; pos < data.size() || pos == 0; pos += kChunk) {
        const bool more = pos + kChunk < data.size();
        const std::string piece = ByteWriter()
                                      .u8(kPacket)
                                      .u16(m_localId)
                                      .u16(to)
                                      .u8(more ? 1 : 0)
                                      .raw(std::string_view(data).substr(pos, kChunk))
                                      .data();
        if (!m_isHost) {
            session.sendApp(-1, piece); // l'hôte relaiera
        } else {
            for (const Player& p : m_players)
                if (p.linkId != 0 && (to == kBroadcast || to == p.linkId))
                    session.sendApp(p.memberId, piece);
        }
        if (!more)
            break;
    }
}

std::vector<LinkPlay::Received> LinkPlay::takeReceived()
{
    std::vector<Received> out;
    out.swap(m_received);
    return out;
}

void LinkPlay::sendTunnelAddresses(Session& session, const std::vector<Address>& addresses)
{
    ByteWriter w;
    w.u8(kTunnel).u8(static_cast<uint8_t>(std::min<size_t>(addresses.size(), 16)));
    for (size_t i = 0; i < addresses.size() && i < 16; ++i)
        w.u32(addresses[i].ip).u16(addresses[i].port);
    // Deux joueurs seulement en GameLink : l'hôte écrit au membre, le membre à l'hôte.
    session.sendApp(-1, w.data());
}

std::optional<std::vector<Address>> LinkPlay::takePeerAddresses()
{
    auto out = std::move(m_peerAddresses);
    m_peerAddresses.reset();
    return out;
}

// --- Fin de partie ---------------------------------------------------------------------

void LinkPlay::stop(Session& session, const std::string& reason)
{
    if (!active())
        return;
    session.sendApp(-1, ByteWriter().u8(kStop).str(reason).data());
    finish(reason);
}

void LinkPlay::finish(const std::string& reason)
{
    m_phase = Phase::Idle;
    m_players.clear();
    m_loaded.clear();
    m_received.clear();
    m_partial.clear();
    m_peerAddresses.reset();
    m_stopped = true;
    m_messages.push_back(reason);
}

std::vector<std::string> LinkPlay::takeMessages()
{
    std::vector<std::string> out;
    out.swap(m_messages);
    return out;
}

bool LinkPlay::takeStopped()
{
    const bool stopped = m_stopped;
    m_stopped = false;
    return stopped;
}

const LinkPlay::Player* LinkPlay::byMember(uint8_t memberId) const
{
    for (const Player& p : m_players)
        if (p.memberId == memberId)
            return &p;
    return nullptr;
}

const LinkPlay::Player* LinkPlay::byLink(uint16_t linkId) const
{
    for (const Player& p : m_players)
        if (p.linkId == linkId)
            return &p;
    return nullptr;
}

} // namespace net
