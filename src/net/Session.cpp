#include "Session.hpp"

#include "../Hash.hpp"
#include "Bytes.hpp"

#include <algorithm>
#include <cstring>

namespace net {

namespace {

// En-tête de tous nos paquets : "EMUN", numéro de salon, type.
constexpr uint8_t kMagic[4] = {'E', 'M', 'U', 'N'};
constexpr size_t kHeader = 9;
constexpr uint8_t kProtocolVersion = 4; // 4 : pertes dans la liste des membres, chat vocal

// L'hôte écoute de préférence sur l'un de ces ports : un ami du même réseau
// local le trouve alors sans connaître son adresse (diffusion).
constexpr uint16_t kLanPortBase = 47800;
constexpr int kLanPorts = 10;

// Serveurs STUN publics et gratuits (on en essaie plusieurs).
const char* const kStunHosts[] = {"stun.l.google.com", "stun1.l.google.com", "stun.cloudflare.com"};
constexpr uint16_t kStunPorts[] = {19302, 19302, 3478};

constexpr Uint64 kProbeIntervalMs = 250;   // "Bonjour" / "perforation" 4 fois par seconde
constexpr Uint64 kPingIntervalMs = 250; // 4 par seconde : assez pour mesurer les pertes
constexpr Uint64 kRosterIntervalMs = 2000; // liste des membres (avec les pings) toutes les 2 s
constexpr Uint64 kTimeoutMs = 10000;       // sans nouvelles pendant 10 s : déconnecté
constexpr Uint64 kResendMs = 400;          // renvoi d'un message fiable sans accusé de réception
constexpr Uint64 kAnswerAfterMs = 5000;    // l'ami propose un code de réponse au bout de 5 s
constexpr Uint64 kPreparationMaxMs = 4000; // on n'attend pas UPnP/STUN plus longtemps

uint32_t read32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool debugLog()
{
    return SDL_getenv("EMU_NET_DEBUG") != nullptr;
}

} // namespace

LinkQuality qualityOf(int pingMs, int lossPct)
{
    if (pingMs < 0)
        return LinkQuality::Unknown;
    const int loss = std::max(0, lossPct);
    if (pingMs < 60 && loss < 1)
        return LinkQuality::Excellent;
    if (pingMs < 120 && loss < 3)
        return LinkQuality::Good;
    if (pingMs < 200 && loss < 8)
        return LinkQuality::Fair;
    return LinkQuality::Poor;
}

const char* qualityLabel(LinkQuality quality)
{
    switch (quality) {
    case LinkQuality::Excellent: return "Excellente";
    case LinkQuality::Good: return "Bonne";
    case LinkQuality::Fair: return "Moyenne";
    case LinkQuality::Poor: return "Mauvaise";
    case LinkQuality::Unknown: break;
    }
    return "Mesure…";
}

Session::Session() = default;

Session::~Session()
{
    leave();
}

// --- Petits outils ------------------------------------------------------------

void Session::event(Event::Type type, std::string text, std::string author)
{
    SDL_Log("Réseau : %s%s%s", author.c_str(), author.empty() ? "" : " : ", text.c_str());
    m_events.push_back({type, std::move(text), std::move(author)});
}

void Session::detail(std::string line)
{
    SDL_Log("Réseau : %s", line.c_str());
    m_details.push_back(std::move(line));
}

std::vector<Session::Event> Session::takeEvents()
{
    std::vector<Event> events;
    events.swap(m_events);
    return events;
}

void Session::setState(State s)
{
    m_state = s;
    m_stateSince = SDL_GetTicks();
}

std::string Session::packet(Packet type, const std::string& payload) const
{
    ByteWriter w;
    w.raw(std::string_view(reinterpret_cast<const char*>(kMagic), 4)).u32(m_sessionId).u8(static_cast<uint8_t>(type)).raw(payload);
    return w.data();
}

void Session::send(const Address& to, Packet type, const std::string& payload)
{
    const std::string p = packet(type, payload);
    sendRaw(to, p.data(), p.size());
    if (debugLog() && type != Packet::Frame && type != Packet::Voice) // 50 à 60 par seconde : trop bavard
        SDL_Log("[net] -> %s type %d (%zu o)", to.toString().c_str(), static_cast<int>(type), p.size());
}

void Session::sendReliable(Link& link, Kind kind, const std::string& data)
{
    // Le message est rangé avec son numéro ; serviceLink() l'envoie, puis le
    // renvoie toutes les 400 ms tant qu'aucun accusé de réception n'arrive.
    const uint32_t seq = link.nextSeq++;
    ByteWriter w;
    w.u32(seq).u8(static_cast<uint8_t>(kind)).raw(data);
    link.unacked[seq] = {packet(Packet::Reliable, w.data()), 0};
}

void Session::serviceLink(Link& link, Uint64 now)
{
    if (now - link.lastPing > kPingIntervalMs) {
        const uint32_t seq = link.pingSeq++;
        link.pingSent[seq % Link::kPingHistory] = now;
        link.pingAnswered[seq % Link::kPingHistory] = false;
        send(link.addr, Packet::Ping, ByteWriter().u32(static_cast<uint32_t>(now)).u32(seq).data());
        link.lastPing = now;
        // Pertes : parmi les pings partis il y a 1 à 6 s (le temps d'avoir
        // leur réponse), combien n'en ont pas eu ?
        int sent = 0, lost = 0;
        for (int i = 0; i < Link::kPingHistory; ++i) {
            const Uint64 at = link.pingSent[i];
            if (at == 0 || now - at < 1000 || now - at > 6000)
                continue;
            ++sent;
            lost += link.pingAnswered[i] ? 0 : 1;
        }
        if (sent >= 4)
            link.lossPct = lost * 100 / sent;
    }
    // Limite de débit : au plus 48 messages (re)partent à chaque image, les
    // plus anciens d'abord. Sans ça, l'envoi d'une sauvegarde d'état de
    // plusieurs Mo partirait d'un coup et saturerait la connexion.
    int budget = 48;
    for (auto& [seq, msg] : link.unacked) {
        if (budget == 0)
            break;
        if (now - msg.lastSent > kResendMs) {
            sendRaw(link.addr, msg.packet.data(), msg.packet.size());
            msg.lastSent = now;
            --budget;
        }
    }
}

void Session::sendApp(int to, const std::string& data)
{
    if (!inSalon())
        return;
    if (!m_isHost) {
        sendReliable(m_hostLink, Kind::App, data);
        return;
    }
    for (auto& [id, peer] : m_peers)
        if (to < 0 || to == id)
            sendReliable(peer.link, Kind::App, data);
}

void Session::sendFrame(int to, const std::string& data)
{
    if (!inSalon())
        return;
    if (!m_isHost) {
        send(m_hostLink.addr, Packet::Frame, data);
        return;
    }
    for (auto& [id, peer] : m_peers)
        if (to < 0 || to == id)
            send(peer.link.addr, Packet::Frame, data);
}

std::vector<Session::FramePacket> Session::takeFrames()
{
    std::vector<FramePacket> frames;
    frames.swap(m_frames);
    return frames;
}

int Session::pingOf(uint8_t memberId) const
{
    if (!m_isHost)
        return memberId == 0 ? m_hostLink.pingMs : -1;
    const auto it = m_peers.find(memberId);
    return it == m_peers.end() ? -1 : it->second.link.pingMs;
}

int Session::lossOf(uint8_t memberId) const
{
    if (!m_isHost)
        return memberId == 0 ? m_hostLink.lossPct : -1;
    const auto it = m_peers.find(memberId);
    return it == m_peers.end() ? -1 : it->second.link.lossPct;
}

void Session::sendVoice(const std::string& data)
{
    if (!inSalon())
        return;
    // [qui parle][la voix] : l'hôte relaie tel quel aux autres membres.
    const std::string p = ByteWriter().u8(m_selfId).raw(data).data();
    if (!m_isHost) {
        send(m_hostLink.addr, Packet::Voice, p);
        return;
    }
    for (auto& [id, peer] : m_peers)
        send(peer.link.addr, Packet::Voice, p);
}

std::vector<Session::FramePacket> Session::takeVoice()
{
    std::vector<FramePacket> voice;
    voice.swap(m_voice);
    return voice;
}

Session::Link* Session::linkFrom(const Address& from)
{
    if (m_isHost) {
        for (auto& [id, peer] : m_peers)
            if (peer.link.addr == from)
                return &peer.link;
        return nullptr;
    }
    return (m_state == State::InSalon && m_hostLink.addr == from) ? &m_hostLink : nullptr;
}

// --- Ouverture du salon ---------------------------------------------------------

bool Session::openSocket(bool lanPort)
{
    leave();
    m_details.clear();
    bool opened = false;
    for (int i = 0; lanPort && i < kLanPorts && !opened; ++i)
        opened = m_socket.open(static_cast<uint16_t>(kLanPortBase + i));
    if (!opened && !m_socket.open(0)) {
        event(Event::Type::Error, "Impossible d'ouvrir un port réseau");
        return false;
    }
    detail("Port local : " + std::to_string(m_socket.localPort()));
    return true;
}

void Session::startStun()
{
    m_stun.emplace();
    m_stunServers.clear();
    for (size_t i = 0; i < std::size(kStunHosts); ++i)
        if (auto a = resolve(kStunHosts[i], kStunPorts[i]))
            m_stunServers.push_back(*a);
    m_stunLastSend = 0;
    m_stunAttempts = 0;
    m_publicAddress.reset();
    m_stunDone = m_stunServers.empty();
    if (m_stunDone)
        detail("Pas d'accès aux serveurs STUN (pas d'Internet ?)");
}

Address Session::bestAddress() const
{
    // L'adresse vue d'Internet (STUN), sinon celle annoncée par la box
    // (UPnP), sinon la première adresse locale (VPN, même box).
    if (m_publicAddress)
        return *m_publicAddress;
    if (m_upnp && m_upnp->succeeded() && m_upnp->externalIp())
        return {m_upnp->externalIp(), m_socket.localPort()};
    for (uint32_t ip : localAddresses())
        if (isPrivate(ip))
            return {ip, m_socket.localPort()};
    return {0x7F000001, m_socket.localPort()};
}

std::vector<Address> Session::ownCandidates() const
{
    // Ordre de préférence : port ouvert par UPnP, adresse publique, puis
    // réseaux locaux (même box, Radmin, Hamachi...).
    std::vector<Address> list;
    auto add = [&](Address a) {
        if (a.valid() && std::find(list.begin(), list.end(), a) == list.end())
            list.push_back(a);
    };
    if (m_upnp && m_upnp->succeeded() && m_upnp->externalIp())
        add({m_upnp->externalIp(), m_socket.localPort()});
    if (m_publicAddress)
        add(*m_publicAddress);
    int locals = 0;
    for (uint32_t ip : localAddresses()) {
        if (isPrivate(ip) && locals < 3) {
            add({ip, m_socket.localPort()});
            ++locals;
        }
    }
    return list;
}

void Session::sendRaw(const Address& to, const void* data, size_t size)
{
    if (m_relay && to.ip == relay::kVirtualIp) {
        // Chiffré de bout en bout, puis confié au relais avec la place du
        // destinataire (côté hôte) ; un joueur n'écrit qu'à l'hôte (place 0).
        const std::string sealed = m_crypto.seal(std::string(static_cast<const char*>(data), size));
        const uint8_t slot = m_isHost ? static_cast<uint8_t>(to.port) : 0;
        const std::string p = relay::packet(relay::Op::Data, m_room, slot, sealed);
        if (m_relayAddr)
            m_socket.sendTo(*m_relayAddr, p.data(), p.size());
        else
            for (const Address& a : m_relayCandidates) // (on ne sait pas encore laquelle répond)
                m_socket.sendTo(a, p.data(), p.size());
        return;
    }
    m_socket.sendTo(to, data, size);
}

void Session::handleRelayPacket(const Address& from, const uint8_t* data, size_t size)
{
    // Seulement ce qui vient du relais, pour notre salon.
    if (std::find(m_relayCandidates.begin(), m_relayCandidates.end(), from) == m_relayCandidates.end() &&
        !(m_relayAddr && *m_relayAddr == from))
        return;
    const auto op = static_cast<relay::Op>(data[4]);
    const uint32_t room = read32(data + 5);
    const uint8_t slot = data[9];
    if (room != m_room)
        return;
    switch (op) {
    case relay::Op::Registered:
        if (m_isHost) {
            if (!m_relayRegistered)
                detail("Relais joint : " + from.toString());
            m_relayAddr = from;
            m_relayRegistered = true;
        }
        break;
    case relay::Op::NoRoom:
        if (!m_isHost && m_state == State::Joining)
            closeSalon("Ce salon n'existe pas (ou plus) sur le relais");
        break;
    case relay::Op::Data: {
        std::string plain;
        if (!m_crypto.open(data + relay::kHeader, size - relay::kHeader, plain)) {
            detail("Paquet du relais rejeté (chiffrement invalide)");
            return;
        }
        if (!m_isHost && !m_relayAddr)
            m_relayAddr = from;
        const Address virtualFrom{relay::kVirtualIp, m_isHost ? static_cast<uint16_t>(slot) : relay::kHostPort};
        handlePacket(virtualFrom, reinterpret_cast<const uint8_t*>(plain.data()), plain.size());
        break;
    }
    default:
        break;
    }
}

void Session::updateRelayHost(Uint64 now)
{
    // Se déclarer au relais (toutes les 500 ms au début, puis toutes les 3 s
    // pour garder le salon en vie).
    const Uint64 interval = m_relayRegistered ? 3000 : 500;
    if (now - m_lastRegister > interval) {
        const std::string p = relay::packet(relay::Op::Register, m_room, 0);
        if (m_relayAddr)
            m_socket.sendTo(*m_relayAddr, p.data(), p.size());
        else
            for (const Address& a : m_relayCandidates)
                m_socket.sendTo(a, p.data(), p.size());
        m_lastRegister = now;
    }
    if (m_state != State::Preparing)
        return;
    if (m_relayRegistered) {
        // Le code d'invitation : les adresses du relais, le salon, le secret. Pas la nôtre.
        std::vector<Address> relayList;
        relayList.push_back(*m_relayAddr);
        for (const Address& a : m_relayCandidates)
            if (!(a == *m_relayAddr))
                relayList.push_back(a);
        m_inviteCode = encodeRelayInvite(m_room, m_secret, relayList);
        m_fullInviteCode = m_inviteCode;
        setState(State::Hosting);
        rebuildMembers();
        event(Event::Type::Joined, "Salon « " + m_salonName + " » prêt (par relais) : partagez le code");
    } else if (now - m_stateSince > 8000) {
        closeSalon("Le relais ne répond pas : vérifiez son code, et qu'il est toujours ouvert");
    }
}

bool Session::hostViaRelay(const std::string& salonName, const std::string& password, const std::string& relayCode)
{
    const std::optional<std::vector<Address>> relayList = decodeRelayCode(relayCode);
    if (!relayList) {
        event(Event::Type::Error, "Ce code de relais n'est pas valide");
        return false;
    }
    if (!openSocket(false))
        return false;
    m_isHost = true;
    m_relay = true;
    m_relayCandidates = *relayList;
    m_relayAddr.reset();
    m_relayRegistered = false;
    m_lastRegister = 0;
    randomBytes(&m_room, sizeof(m_room));
    if (m_room == 0)
        m_room = 1;
    randomBytes(m_secret.data(), m_secret.size());
    m_crypto.init(m_secret);
    m_sessionId = (m_room & 0xFFFFFF) ? (m_room & 0xFFFFFF) : 1;
    m_passwordHash = password.empty() ? 0 : Hasher64::of(password, m_sessionId);
    m_salonName = salonName.empty() ? "Salon de " + m_nickname : salonName.substr(0, 40);
    m_selfId = 0;
    m_stun.reset();
    m_stunDone = true;
    setState(State::Preparing);
    detail("Connexion au relais…");
    return true;
}

bool Session::host(const std::string& salonName, const std::string& password)
{
    if (!openSocket(true))
        return false;
    m_isHost = true;
    m_sessionId = SDL_rand_bits() & 0xFFFFFF; // 24 bits : il doit tenir dans le code court
    if (m_sessionId == 0)
        m_sessionId = 1;
    // Le mot de passe ne circule jamais : seule son empreinte, mélangée au
    // numéro du salon, est comparée.
    m_passwordHash = password.empty() ? 0 : Hasher64::of(password, m_sessionId);
    m_salonName = salonName.empty() ? "Salon de " + m_nickname : salonName.substr(0, 40);
    m_selfId = 0;
    startStun();
    m_upnp = std::make_unique<PortMapping>();
    m_upnp->start(m_socket.localPort());
    setState(State::Preparing);
    detail("Recherche de l'adresse publique et ouverture du port…");
    return true;
}

bool Session::join(const std::string& code, const std::string& password)
{
    const std::optional<Invite> invite = decodeInvite(code);
    if (!invite || invite->candidates.empty()) {
        event(Event::Type::Error, "Ce code n'est pas valide : vérifiez qu'il est complet");
        return false;
    }
    if (!openSocket(false))
        return false;
    m_isHost = false;
    if (invite->relay) {
        // Salon par relais : on ne connaît que le relais ; l'hôte est "virtuel".
        m_relay = true;
        m_room = invite->session;
        m_secret = invite->secret;
        m_crypto.init(m_secret);
        m_relayCandidates = invite->candidates;
        m_relayAddr.reset();
        m_sessionId = (m_room & 0xFFFFFF) ? (m_room & 0xFFFFFF) : 1;
        m_passwordHash = password.empty() ? 0 : Hasher64::of(password, m_sessionId);
        m_nonce = SDL_rand_bits() | 1;
        m_targets = {Address{relay::kVirtualIp, relay::kHostPort}};
        m_lanTargets.clear();
        m_stun.reset();
        m_stunDone = true;
        setState(State::Joining);
        detail("Connexion par le relais (" + std::to_string(m_relayCandidates.size()) + " adresse(s))");
        return true;
    }
    m_sessionId = invite->session;
    m_passwordHash = password.empty() ? 0 : Hasher64::of(password, m_sessionId);
    m_nonce = SDL_rand_bits() | 1;
    m_targets = invite->candidates;
    // Si l'hôte est sur le même réseau local (même box, Radmin, Hamachi) ou
    // sur ce PC, on le trouve par diffusion, sans connaître son adresse.
    m_lanTargets.clear();
    std::vector<uint32_t> lan = broadcastAddresses();
    lan.push_back(0x7F000001); // 127.0.0.1 : ce PC
    for (uint32_t ip : lan)
        for (int i = 0; i < kLanPorts; ++i)
            m_lanTargets.push_back({ip, static_cast<uint16_t>(kLanPortBase + i)});
    startStun(); // pour pouvoir proposer un code de réponse
    setState(State::Joining);
    for (const Address& a : m_targets)
        detail("Essai vers " + a.toString());
    detail("Recherche sur le réseau local");
    return true;
}

bool Session::acceptAnswer(const std::string& code)
{
    const std::optional<Invite> answer = decodeInvite(code);
    if (!m_isHost || !answer || answer->session != m_sessionId) {
        event(Event::Type::Error, "Ce code de réponse ne correspond pas à ce salon");
        return false;
    }
    for (const Address& a : answer->candidates)
        if (std::find(m_targets.begin(), m_targets.end(), a) == m_targets.end())
            m_targets.push_back(a);
    detail("Code de réponse reçu : perforation vers " + std::to_string(answer->candidates.size()) + " adresse(s)");
    return true;
}

void Session::leave()
{
    // On prévient les autres, sans attendre de réponse.
    if (m_state == State::Hosting)
        for (auto& [id, peer] : m_peers)
            send(peer.link.addr, Packet::Bye);
    if (m_state == State::InSalon)
        send(m_hostLink.addr, Packet::Bye);

    m_socket.close();
    m_relay = false;
    m_relayCandidates.clear();
    m_relayAddr.reset();
    m_relayRegistered = false;
    m_crypto.reset();
    m_upnp.reset(); // referme le port sur la box
    m_upnpReported = false;
    m_stun.reset();
    m_state = State::Idle;
    m_inviteCode.clear();
    m_fullInviteCode.clear();
    m_answerCode.clear();
    m_targets.clear();
    m_lanTargets.clear();
    m_voice.clear();
    m_peers.clear();
    m_hostLink = {};
    m_members.clear();
    m_game.reset();
    m_localStatus = GameStatus::Unknown;
    m_lastRosterSeq = m_lastGameSeq = 0;
}

void Session::closeSalon(const std::string& reason)
{
    leave();
    event(Event::Type::Closed, reason);
}

// --- Actions -------------------------------------------------------------------------

void Session::sendChat(const std::string& text)
{
    if (!inSalon() || text.empty())
        return;
    const std::string t = text.substr(0, 400);
    if (m_isHost) {
        // L'hôte envoie directement à tout le monde, en se citant comme auteur.
        const std::string data = ByteWriter().u8(0).raw(t).data();
        for (auto& [id, peer] : m_peers)
            sendReliable(peer.link, Kind::Chat, data);
    } else {
        sendReliable(m_hostLink, Kind::Chat, t);
    }
}

std::string Session::gameMessage() const
{
    ByteWriter w;
    if (m_game)
        w.u64(m_game->hash).u64(m_game->size).str(m_game->systemId).str(m_game->title).u8(m_game->link ? 1 : 0);
    else
        w.u64(0).u64(0).str("").str("");
    return w.data();
}

void Session::selectGame(const SalonGame& game)
{
    if (!m_isHost || m_state != State::Hosting)
        return;
    m_game = game;
    m_localStatus = GameStatus::Unknown; // l'hôte aussi revérifie
    // Chacun doit revérifier pour ce nouveau jeu.
    for (auto& [id, peer] : m_peers) {
        peer.status = GameStatus::Checking;
        sendReliable(peer.link, Kind::Game, gameMessage());
    }
    event(Event::Type::GameChanged, "Jeu choisi : " + game.title + (game.link ? " (chacun sa console)" : ""));
    broadcastRoster();
}

void Session::kick(uint8_t memberId)
{
    auto it = m_peers.find(memberId);
    if (!m_isHost || it == m_peers.end())
        return;
    // On envoie l'avis trois fois tout de suite (pas de renvoi possible
    // ensuite : l'ami est retiré de la liste immédiatement).
    Link& link = it->second.link;
    const uint32_t seq = link.nextSeq++;
    const std::string p = packet(Packet::Reliable, ByteWriter().u32(seq).u8(static_cast<uint8_t>(Kind::Kick)).data());
    for (int i = 0; i < 3; ++i)
        sendRaw(link.addr, p.data(), p.size());
    removePeer(memberId, it->second.name + " a été retiré du salon");
}

void Session::setLocalStatus(GameStatus status)
{
    if (m_localStatus == status)
        return;
    m_localStatus = status;
    if (m_isHost) {
        broadcastRoster();
    } else if (m_state == State::InSalon) {
        sendReliable(m_hostLink, Kind::Status, ByteWriter().u8(static_cast<uint8_t>(status)).data());
        rebuildMembers();
    }
}

// --- Liste des membres -------------------------------------------------------------

void Session::rebuildMembers()
{
    if (m_isHost) {
        m_members.clear();
        m_members.push_back({0, m_nickname, -1, -1, m_localStatus, true, true});
        for (const auto& [id, peer] : m_peers)
            m_members.push_back({id, peer.name, peer.link.pingMs, peer.link.lossPct, peer.status, false, false});
    } else {
        // Un membre connaît sa propre vérification avant que l'hôte ne la
        // renvoie, et mesure lui-même sa liaison avec l'hôte.
        for (Member& m : m_members) {
            m.self = (m.id == m_selfId);
            if (m.self)
                m.status = m_localStatus;
            if (m.host) {
                m.pingMs = m_hostLink.pingMs;
                m.lossPct = m_hostLink.lossPct;
            }
        }
    }
}

void Session::broadcastRoster()
{
    rebuildMembers();
    ByteWriter w;
    w.u8(static_cast<uint8_t>(m_members.size()));
    for (const Member& m : m_members)
        w.u8(m.id).u16(m.pingMs < 0 ? 0xFFFF : static_cast<uint16_t>(std::min(m.pingMs, 0xFFFE)))
            .u8(m.lossPct < 0 ? 0xFF : static_cast<uint8_t>(std::min(m.lossPct, 100)))
            .u8(static_cast<uint8_t>(m.status)).u8(m.host ? 1 : 0).str(m.name);
    for (auto& [id, peer] : m_peers)
        sendReliable(peer.link, Kind::Roster, w.data());
    m_lastRoster = SDL_GetTicks();
}

void Session::removePeer(uint8_t id, const std::string& reason)
{
    if (m_peers.erase(id)) {
        event(Event::Type::MemberLeft, reason);
        broadcastRoster();
    }
}

// --- Boucle -------------------------------------------------------------------------------

void Session::updatePreparation()
{
    const Uint64 now = SDL_GetTicks();

    // STUN : on (re)pose la question toutes les 500 ms, 6 fois au plus.
    if (m_stun && !m_stunDone && now - m_stunLastSend > 500) {
        if (m_stunAttempts >= 6) {
            m_stunDone = true;
            detail("Adresse publique introuvable (les serveurs STUN ne répondent pas)");
        } else {
            const std::vector<uint8_t> request = m_stun->bytes();
            for (const Address& server : m_stunServers)
                m_socket.sendTo(server, request.data(), request.size());
            m_stunLastSend = now;
            ++m_stunAttempts;
        }
    }

    if (m_upnp && m_upnp->finished() && !m_upnpReported) {
        m_upnpReported = true;
        detail("Box : " + m_upnp->message());
    }

    // Le code est prêt quand STUN et UPnP ont fini (ou au bout de 4 s).
    if (m_state == State::Preparing) {
        const bool upnpDone = !m_upnp || m_upnp->finished();
        if ((m_stunDone && upnpDone) || now - m_stateSince > kPreparationMaxMs) {
            m_inviteCode = encodeShortInvite(m_sessionId, bestAddress());
            m_fullInviteCode = encodeInvite({m_sessionId, ownCandidates()});
            setState(State::Hosting);
            rebuildMembers();
            event(Event::Type::Joined, "Salon « " + m_salonName + " » prêt : partagez le code");
        }
    }
    // Côté ami : au bout de quelques secondes sans succès, on propose un
    // code de réponse à renvoyer à l'hôte.
    if (m_state == State::Joining && m_answerCode.empty() && now - m_stateSince > kAnswerAfterMs &&
        (m_stunDone || now - m_stateSince > kPreparationMaxMs + kAnswerAfterMs)) {
        m_answerCode = encodeShortInvite(m_sessionId, bestAddress());
        detail("Toujours rien : un code de réponse est disponible");
    }
}

void Session::update()
{
    if (m_state == State::Idle || !m_socket.isOpen())
        return;

    // 1. Lire tout ce qui est arrivé.
    uint8_t buffer[2048];
    Address from;
    while (size_t n = m_socket.receive(from, buffer, sizeof(buffer))) {
        if (m_relay && n >= relay::kHeader && std::memcmp(buffer, relay::kMagic, 4) == 0) {
            handleRelayPacket(from, buffer, n);
            if (m_state == State::Idle)
                return;
            continue;
        }
        if (StunRequest::looksLikeStun(buffer, n)) {
            if (m_stun && !m_publicAddress)
                if (auto a = m_stun->parseResponse(buffer, n)) {
                    m_publicAddress = a;
                    m_stunDone = true;
                    detail("Adresse publique : " + a->toString());
                }
            continue;
        }
        handlePacket(from, buffer, n);
        if (m_state == State::Idle)
            return; // le salon vient d'être fermé
    }

    if (m_relay && m_isHost)
        updateRelayHost(SDL_GetTicks());
    else
        updatePreparation();

    // On lit l'heure APRÈS avoir traité les paquets reçus. Sinon,
    // "maintenant - dernier paquet reçu" peut valoir -1 ms... et sur un
    // entier non signé (Uint64), -1 devient un nombre gigantesque : la
    // connexion était jugée perdue dans l'image même où elle s'établissait.
    const Uint64 now = SDL_GetTicks();

    // La liste des membres affichée suit les pings et pertes mesurés.
    if (inSalon() && now - m_lastMembersRefresh > kPingIntervalMs) {
        rebuildMembers();
        m_lastMembersRefresh = now;
    }

    // 2. Envoyer ce qui doit l'être selon l'état.
    switch (m_state) {
    case State::Joining:
        if (now - m_lastProbe > kProbeIntervalMs) {
            const std::string hello = ByteWriter().u8(kProtocolVersion).u32(m_nonce).u64(m_passwordHash).str(m_nickname).data();
            for (const Address& a : m_targets)
                send(a, Packet::Hello, hello);
            // Le réseau local : deux fois par seconde suffisent.
            if ((now / kProbeIntervalMs) % 2 == 0) {
                const std::string p = packet(Packet::Hello, hello);
                for (const Address& a : m_lanTargets)
                    sendRaw(a, p.data(), p.size());
            }
            m_lastProbe = now;
        }
        break;
    case State::Hosting: {
        if (!m_targets.empty() && now - m_lastProbe > kProbeIntervalMs) {
            for (const Address& a : m_targets)
                send(a, Packet::Punch);
            m_lastProbe = now;
        }
        std::vector<std::pair<uint8_t, std::string>> lost;
        for (auto& [id, peer] : m_peers) {
            serviceLink(peer.link, now);
            if (now - peer.link.lastReceived > kTimeoutMs)
                lost.push_back({id, peer.name + " ne répond plus"});
        }
        for (const auto& [id, reason] : lost)
            removePeer(id, reason);
        if (!m_peers.empty() && now - m_lastRoster > kRosterIntervalMs)
            broadcastRoster(); // pour rafraîchir les pings affichés
        break;
    }
    case State::InSalon:
        serviceLink(m_hostLink, now);
        if (now - m_hostLink.lastReceived > kTimeoutMs)
            closeSalon("L'hôte ne répond plus : connexion perdue");
        break;
    default:
        break;
    }
}

// --- Réception -------------------------------------------------------------------------

void Session::handlePacket(const Address& from, const uint8_t* data, size_t size)
{
    if (debugLog() && (size < kHeader || data[8] != static_cast<uint8_t>(Packet::Frame)))
        SDL_Log("[net] <- %s type %d (%zu o)", from.toString().c_str(), size >= kHeader ? data[8] : -1, size);
    // On ignore tout ce qui ne vient pas de notre salon.
    if (size < kHeader || std::memcmp(data, kMagic, 4) != 0 || read32(data + 4) != m_sessionId)
        return;
    const auto type = static_cast<Packet>(data[8]);
    const std::string payload(reinterpret_cast<const char*>(data + kHeader), size - kHeader);
    Link* link = linkFrom(from);
    if (link)
        link->lastReceived = SDL_GetTicks();

    switch (type) {
    case Packet::Hello:
        if (m_isHost)
            handleHello(from, payload);
        break;
    case Packet::Welcome:
        if (!m_isHost && m_state == State::Joining) {
            ByteReader r(payload);
            m_selfId = r.u8();
            m_salonName = r.str();
            m_hostLink = {};
            m_hostLink.addr = from;
            m_hostLink.lastReceived = SDL_GetTicks();
            m_targets.clear();
            setState(State::InSalon);
            detail(m_relay ? std::string("Relié par le relais (adresse de l'hôte cachée)") : "Relié à " + from.toString());
            event(Event::Type::Joined, "Vous avez rejoint « " + m_salonName + " »");
        }
        break;
    case Packet::Reject:
        if (!m_isHost && m_state == State::Joining)
            closeSalon(ByteReader(payload).str());
        break;
    case Packet::Punch:
        // La box de l'hôte s'est ouverte vers nous : on y frappe aussitôt.
        if (!m_isHost && m_state == State::Joining &&
            std::find(m_targets.begin(), m_targets.end(), from) == m_targets.end())
            m_targets.push_back(from);
        break;
    case Packet::Ping:
        if (link)
            send(from, Packet::Pong, payload);
        break;
    case Packet::Pong:
        if (link && payload.size() >= 4) {
            const auto* p = reinterpret_cast<const uint8_t*>(payload.data());
            link->pingMs = static_cast<int>(static_cast<uint32_t>(SDL_GetTicks()) - read32(p));
            if (payload.size() >= 8)
                link->pingAnswered[read32(p + 4) % Link::kPingHistory] = true;
        }
        break;
    case Packet::Voice:
        if (link && !payload.empty()) {
            // L'hôte retrouve qui parle d'après l'adresse (on ne croit pas le
            // numéro écrit dans le paquet), et relaie aux autres membres.
            uint8_t speaker = static_cast<uint8_t>(payload[0]);
            if (m_isHost) {
                for (const auto& [id, peer] : m_peers)
                    if (&peer.link == link)
                        speaker = id;
                const std::string relay = ByteWriter().u8(speaker).raw(std::string_view(payload).substr(1)).data();
                for (auto& [id, peer] : m_peers)
                    if (id != speaker)
                        send(peer.link.addr, Packet::Voice, relay);
            }
            m_voice.push_back({speaker, payload.substr(1)});
        }
        break;
    case Packet::Reliable: {
        if (!link || payload.size() < 5)
            break;
        ByteReader r(payload);
        const uint32_t seq = r.u32();
        const auto kind = static_cast<Kind>(r.u8());
        send(from, Packet::Ack, ByteWriter().u32(seq).data()); // accusé de réception, même pour un doublon
        if (link->received.insert(seq).second)
            handleMessage(from, kind, r.rest(), seq);
        break;
    }
    case Packet::Ack:
        if (link && payload.size() >= 4)
            link->unacked.erase(read32(reinterpret_cast<const uint8_t*>(payload.data())));
        break;
    case Packet::Frame:
        if (link) {
            uint8_t sender = 0;
            if (m_isHost)
                for (const auto& [id, peer] : m_peers)
                    if (&peer.link == link)
                        sender = id;
            m_frames.push_back({sender, payload});
        }
        break;
    case Packet::Bye:
        if (m_isHost) {
            for (auto& [id, peer] : m_peers)
                if (peer.link.addr == from) {
                    removePeer(id, peer.name + " a quitté le salon");
                    break;
                }
        } else if (link) {
            m_state = State::Idle; // pas de "Bye" en retour
            closeSalon("L'hôte a fermé le salon");
        }
        break;
    }
}

void Session::handleHello(const Address& from, const std::string& payload)
{
    if (m_state != State::Hosting && m_state != State::Preparing)
        return;
    ByteReader r(payload);
    const uint8_t version = r.u8();
    const uint32_t nonce = r.u32();
    const uint64_t password = r.u64();
    const std::string name = r.str().substr(0, 32);
    if (!r.ok())
        return;
    auto reject = [&](const char* reason) { send(from, Packet::Reject, ByteWriter().str(reason).data()); };

    // Déjà membre (il n'a peut-être pas reçu notre "Bienvenue") ?
    for (auto& [id, peer] : m_peers) {
        if (peer.nonce == nonce) {
            if (peer.link.addr == from) // on ignore les autres chemins du même ami
                send(from, Packet::Welcome, ByteWriter().u8(id).str(m_salonName).data());
            return;
        }
    }
    if (version != kProtocolVersion)
        return reject("Versions de l'émulateur différentes : mettez-le à jour tous les deux");
    if (m_passwordHash != 0 && password != m_passwordHash)
        return reject("Mot de passe incorrect");
    if (static_cast<int>(m_peers.size()) + 1 >= kMaxMembers)
        return reject("Le salon est complet");

    // Nouveau membre : le plus petit numéro libre.
    uint8_t id = 1;
    while (m_peers.count(id))
        ++id;
    Peer& peer = m_peers[id];
    peer.id = id;
    peer.nonce = nonce;
    peer.name = name.empty() ? "Ami " + std::to_string(id) : name;
    peer.status = m_game ? GameStatus::Checking : GameStatus::Unknown;
    peer.link.addr = from;
    peer.link.lastReceived = SDL_GetTicks();
    std::erase(m_targets, from); // plus besoin de "percer" vers lui

    send(from, Packet::Welcome, ByteWriter().u8(id).str(m_salonName).data());
    event(Event::Type::MemberJoined, peer.name + " a rejoint le salon");
    if (m_game)
        sendReliable(peer.link, Kind::Game, gameMessage());
    broadcastRoster();
}

void Session::handleMessage(const Address& from, Kind kind, const std::string& data, uint32_t seq)
{
    ByteReader r(data);
    if (m_isHost) {
        // Messages d'un membre vers l'hôte.
        Peer* sender = nullptr;
        for (auto& [id, peer] : m_peers)
            if (peer.link.addr == from)
                sender = &peer;
        if (!sender)
            return;
        if (kind == Kind::Chat) {
            const std::string text = r.rest();
            event(Event::Type::Chat, text, sender->name);
            // L'hôte relaie à tous les autres membres.
            const std::string relay = ByteWriter().u8(sender->id).raw(text).data();
            for (auto& [id, peer] : m_peers)
                if (id != sender->id)
                    sendReliable(peer.link, Kind::Chat, relay);
        } else if (kind == Kind::Status) {
            sender->status = static_cast<GameStatus>(r.u8());
            broadcastRoster();
        } else if (kind == Kind::App) {
            m_events.push_back({Event::Type::AppMessage, {}, {}, sender->id, r.rest()});
        }
        return;
    }

    // Messages de l'hôte vers un membre.
    switch (kind) {
    case Kind::Chat: {
        const uint8_t author = r.u8();
        std::string name = "?";
        for (const Member& m : m_members)
            if (m.id == author)
                name = m.name;
        event(Event::Type::Chat, r.rest(), name);
        break;
    }
    case Kind::Roster: {
        // Les listes peuvent arriver dans le désordre : on garde la plus récente.
        if (seq < m_lastRosterSeq)
            break;
        m_lastRosterSeq = seq;
        std::vector<Member> members(r.u8());
        for (Member& m : members) {
            m.id = r.u8();
            const uint16_t ping = r.u16();
            m.pingMs = ping == 0xFFFF ? -1 : ping;
            const uint8_t loss = r.u8();
            m.lossPct = loss == 0xFF ? -1 : loss;
            m.status = static_cast<GameStatus>(r.u8());
            m.host = r.u8() != 0;
            m.name = r.str();
        }
        if (r.ok()) {
            m_members = std::move(members);
            rebuildMembers();
        }
        break;
    }
    case Kind::Game: {
        if (seq < m_lastGameSeq)
            break;
        m_lastGameSeq = seq;
        SalonGame g;
        g.hash = r.u64();
        g.size = r.u64();
        g.systemId = r.str();
        g.title = r.str();
        if (!r.ok())
            break;
        g.link = r.u8() == 1;
        if (g.hash == 0)
            m_game.reset();
        else
            m_game = g;
        m_localStatus = GameStatus::Unknown; // à revérifier
        event(Event::Type::GameChanged, !g.hash  ? std::string("L'hôte n'a plus de jeu choisi")
                                        : g.link ? "L'hôte a choisi " + g.title + " : chacun sa console, avec son propre jeu et sa sauvegarde"
                                                 : "L'hôte a choisi " + g.title);
        break;
    }
    case Kind::Kick:
        closeSalon("Vous avez été retiré du salon par l'hôte");
        break;
    case Kind::App:
        m_events.push_back({Event::Type::AppMessage, {}, {}, 0, r.rest()});
        break;
    case Kind::Status:
        break;
    }
}

} // namespace net
