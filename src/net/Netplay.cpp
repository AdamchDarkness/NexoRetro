#include "Netplay.hpp"

#include "../Hash.hpp"
#include "Bytes.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace net {

namespace {

// Types de messages du netplay (premier octet).
constexpr char kStart = 'S';
constexpr char kLoaded = 'L';
constexpr char kChunk = 'D';
constexpr char kApplied = 'R';
constexpr char kGo = 'G';
constexpr char kStop = 'X';
constexpr char kCheck = 'C';
constexpr char kWarning = 'W';

constexpr size_t kChunkSize = 1000;     // morceaux de la sauvegarde d'état
constexpr uint32_t kMaxPerPacket = 32;  // images de touches par paquet au plus
constexpr uint32_t kCheckEvery = 120;   // contrôle de désynchronisation (2 s)
// Au-delà de cette taille, photographier l'état à chaque image coûte trop
// cher : on passe en mode "délai" (rollback de 0 image).
constexpr size_t kMaxRollbackState = 8u * 1024 * 1024;

void writeInput(ByteWriter& w, const PadInput& in)
{
    w.u16(in.buttons);
    for (int16_t a : in.analog)
        w.u16(static_cast<uint16_t>(a));
}

PadInput readInput(ByteReader& r)
{
    PadInput in;
    in.buttons = r.u16();
    for (int16_t& a : in.analog)
        a = static_cast<int16_t>(r.u16());
    return in;
}

} // namespace

// --- Démarrage ------------------------------------------------------------------------

bool Netplay::hostStart(Session& session, std::string& error)
{
    if (!session.isHost() || !session.game()) {
        error = "Choisissez d'abord un jeu";
        return false;
    }
    const auto& members = session.members();
    if (members.size() < 2) {
        error = "Il faut au moins un ami dans le salon";
        return false;
    }
    if (members.size() > kNetPlayers) {
        error = "4 joueurs au plus pour une partie";
        return false;
    }
    for (const Member& m : members) {
        if (m.status != GameStatus::Ready) {
            error = m.name + " n'a pas le bon fichier de jeu";
            return false;
        }
    }

    m_players.clear();
    for (const Member& m : members)
        m_players.push_back({m.id, static_cast<uint8_t>(m_players.size()), m.name});
    m_isHost = true;
    m_localPort = 0;
    m_loaded.clear();
    m_applied.clear();
    m_launched = false;
    m_stopped = false;
    m_desyncs = m_resyncs = m_rollbackCount = m_deepestRollback = 0;
    m_generation = 0;
    m_phase = Phase::Loading;

    ByteWriter w;
    w.u8(kStart).u8(static_cast<uint8_t>(m_players.size()));
    for (const Player& p : m_players)
        w.u8(p.memberId).u8(p.port).str(p.name);
    session.sendApp(-1, w.data());
    m_messages.push_back("Lancement de la partie en ligne");
    return true;
}

void Netplay::launched(Session& session, bool ok, const std::string& coreInfo, bool rollbackSafe, const std::string& error)
{
    m_launched = true;
    m_localCore = coreInfo;
    m_rollbackSafe = rollbackSafe;
    if (!ok) {
        stop(session, "Impossible de lancer le jeu chez " + nameOf(session.selfId()) + " : " + error);
        return;
    }
    if (!m_isHost) {
        session.sendApp(-1, ByteWriter().u8(kLoaded).u8(1).str(coreInfo).str("").data());
        m_phase = Phase::Syncing;
    }
}

void Netplay::hostSendState(Session& session, const std::string& state)
{
    m_hostNeedsState = false;
    m_stateSize = state.size();
    // La photographie part en morceaux de 1000 octets (un paquet UDP ne
    // doit pas dépasser ~1200 octets pour traverser Internet sans souci).
    for (size_t offset = 0; offset < state.size(); offset += kChunkSize) {
        ByteWriter w;
        w.u8(kChunk).u8(m_generation).u32(static_cast<uint32_t>(state.size())).u32(static_cast<uint32_t>(offset));
        w.raw(std::string_view(state).substr(offset, kChunkSize));
        session.sendApp(-1, w.data());
    }
    if (m_generation == 0)
        m_messages.push_back("Envoi de la partie aux joueurs (" + std::to_string(state.size() / 1024) + " Ko)");
}

std::optional<std::string> Netplay::takeReceivedState()
{
    if (!m_stateComplete)
        return std::nullopt;
    m_stateComplete = false;
    return std::move(m_state);
}

void Netplay::stateApplied(Session& session)
{
    session.sendApp(-1, ByteWriter().u8(kApplied).data());
}

void Netplay::startRunning(uint8_t delay, uint8_t maxRollback)
{
    m_delay = delay;
    m_maxRollback = maxRollback;
    m_frame = 0;
    // Les "délai" premières images n'ont pas de touches : manette au repos.
    for (Stream& s : m_streams) {
        s.inputs.clear();
        for (uint32_t f = 0; f < delay; ++f)
            s.inputs[f] = PadInput{};
        s.next = delay;
    }
    m_used.clear();
    m_states.assign(static_cast<size_t>(maxRollback) + 3, {std::numeric_limits<uint32_t>::max(), std::string()});
    m_rollbackFrom.reset();
    m_pendingCheckpoints.clear();
    m_fingerprints.clear();
    m_hashes.clear();
    m_pendingChecks.clear();
    m_clients.clear();
    for (const Player& p : m_players)
        if (p.memberId != 0) {
            ClientSync c;
            c.haveNext.fill(delay);
            m_clients[p.memberId] = c;
        }
    m_hostHasMine = delay;
    m_hostFrame = 0;
    m_ticks = 0;
    m_stalledSince = 0;
    m_delayed.clear();
    m_phase = Phase::Running;
    m_messages.push_back(maxRollback > 0 ? "C'est parti ! (rollback " + std::to_string(maxRollback) + " images)"
                                         : "C'est parti ! (mode délai : " + std::to_string(delay) + " images)");
}

// --- Messages ------------------------------------------------------------------------------

void Netplay::onAppMessage(Session& session, uint8_t from, const std::string& data)
{
    if (data.empty())
        return;
    ByteReader r(data);
    const char type = static_cast<char>(r.u8());

    switch (type) {
    case kStart: {
        if (session.isHost())
            break;
        m_isHost = false;
        m_players.resize(r.u8());
        for (Player& p : m_players) {
            p.memberId = r.u8();
            p.port = r.u8();
            p.name = r.str();
        }
        m_localPort = portOf(session.selfId());
        m_launched = false;
        m_stopped = false;
        m_desyncs = m_resyncs = m_rollbackCount = m_deepestRollback = 0;
        m_generation = 0;
        m_state.clear();
        m_stateChunks.clear();
        m_stateReceived = 0;
        m_stateComplete = false;
        m_phase = Phase::Loading;
        m_messages.push_back("L'hôte lance la partie");
        break;
    }
    case kLoaded: {
        if (!m_isHost || m_phase != Phase::Loading)
            break;
        const bool ok = r.u8() != 0;
        const std::string core = r.str();
        const std::string error = r.str();
        if (!ok) {
            stop(session, "Impossible de lancer le jeu chez " + nameOf(from) + " : " + error);
            break;
        }
        m_loaded[from] = core;
        break;
    }
    case kChunk: {
        if (m_isHost || m_phase == Phase::Idle || m_phase == Phase::Loading)
            break;
        const uint8_t generation = r.u8();
        const uint32_t total = r.u32();
        const uint32_t offset = r.u32();
        const std::string bytes = r.rest();
        if (total == 0 || total > 512u * 1024 * 1024 || offset + bytes.size() > total)
            break;
        if (generation != m_generation) {
            // L'hôte a vu nos parties diverger et renvoie la sienne (nouvelle
            // "manche") : on s'arrête et on la recharge, comme au lancement.
            m_generation = generation;
            m_state.clear();
            m_stateComplete = false;
            if (m_phase == Phase::Running) {
                m_phase = Phase::Syncing;
                ++m_resyncs;
                m_messages.push_back("Vos parties avaient divergé : resynchronisation avec l'hôte…");
            }
        }
        if (m_state.size() != total) {
            m_state.assign(total, '\0');
            m_stateChunks.assign((total + kChunkSize - 1) / kChunkSize, false);
            m_stateReceived = 0;
        }
        const size_t index = offset / kChunkSize;
        if (!m_stateChunks[index]) {
            m_stateChunks[index] = true;
            std::copy(bytes.begin(), bytes.end(), m_state.begin() + offset);
            m_stateReceived += bytes.size();
            if (m_stateReceived == total)
                m_stateComplete = true;
        }
        break;
    }
    case kApplied:
        if (m_isHost && m_phase == Phase::Syncing) {
            m_applied.insert(from);
            if (m_applied.size() + 1 == m_players.size()) {
                // Réglages selon le ping le plus mauvais (et la latence
                // simulée des tests).
                int worstPing = m_simulatedLatencyMs;
                for (const Member& m : session.members())
                    worstPing = std::max(worstPing, m.pingMs + m_simulatedLatencyMs);
                const int pingFrames = static_cast<int>(std::ceil(worstPing / 16.7));
                uint8_t delay = 0, rollback = 0;
                // EMU_NET_FORCE_DELAY : forcer le mode délai (pour comparer, en test).
                if (m_stateSize > kMaxRollbackState || !m_rollbackSafe || SDL_getenv("EMU_NET_FORCE_DELAY")) {
                    // Console trop lourde : mode délai, assez pour couvrir l'aller-retour.
                    delay = static_cast<uint8_t>(std::clamp(static_cast<int>(std::ceil((worstPing + 25.0) / 16.7)), 2, 12));
                } else {
                    // Rollback : 1 ou 2 images de délai, et de quoi revenir
                    // en arrière un peu plus loin que l'aller-retour.
                    delay = worstPing > 80 ? 2 : 1;
                    rollback = static_cast<uint8_t>(std::clamp(pingFrames + 3, 4, 12));
                }
                session.sendApp(-1, ByteWriter().u8(kGo).u8(delay).u8(rollback).data());
                startRunning(delay, rollback);
            }
        }
        break;
    case kGo:
        if (!m_isHost && m_phase == Phase::Syncing) {
            const uint8_t delay = r.u8();
            const uint8_t rollback = r.u8();
            startRunning(delay, rollback);
        }
        break;
    case kStop: {
        const std::string reason = r.str();
        if (m_isHost) {
            // Un membre quitte : on arrête la partie pour tout le monde.
            const std::string text = reason.empty() ? nameOf(from) + " a quitté la partie" : reason;
            session.sendApp(-1, ByteWriter().u8(kStop).str(text).data());
            finish(text);
        } else {
            finish(reason);
        }
        break;
    }
    case kCheck:
        if (m_isHost && m_phase == Phase::Running && r.u8() == m_generation) {
            const uint32_t frame = r.u32();
            checkHash(session, frame, from, r.u64());
        }
        break;
    case kWarning:
        m_messages.push_back(r.str());
        break;
    default:
        break;
    }
}

void Netplay::onMemberLeft(Session& session, const std::string& reason)
{
    if (m_isHost && active()) {
        session.sendApp(-1, ByteWriter().u8(kStop).str(reason).data());
        finish(reason);
    }
}

void Netplay::onSalonClosed(const std::string& reason)
{
    if (active())
        finish(reason);
}

void Netplay::update(Session& session)
{
    // Hôte, pendant le chargement : tout le monde a-t-il démarré le jeu ?
    if (m_isHost && m_phase == Phase::Loading && m_launched && m_loaded.size() + 1 == m_players.size()) {
        // Il faut le même émulateur, dans la même version, partout.
        for (const auto& [id, core] : m_loaded) {
            if (core != m_localCore) {
                // (Le texte contient " · options xxxx" si des options du core ont été changées.)
                stop(session, "Émulateurs différents (version ou options du core) : " + nameOf(id) + " utilise " + core +
                                  ", l'hôte " + m_localCore);
                return;
            }
        }
        m_phase = Phase::Syncing;
        m_hostNeedsState = true;
    }
    if (m_phase != Phase::Running)
        session.takeFrames(); // rien à en faire pour l'instant
}

// --- Touches confirmées et devinées -------------------------------------------------------------

void Netplay::confirm(int port, uint32_t frame, const PadInput& input)
{
    if (port < 0 || port >= kNetPlayers)
        return;
    Stream& s = m_streams[port];
    if (frame < s.next || s.inputs.count(frame))
        return; // déjà connue
    s.inputs[frame] = input;
    while (s.inputs.count(s.next))
        ++s.next;
    // Cette image a déjà été calculée avec une touche DEVINÉE : si on s'est
    // trompé, il faudra revenir en arrière jusqu'à elle.
    if (frame < m_frame) {
        const auto used = m_used.find(frame);
        if (used != m_used.end() && !(used->second[port] == input))
            m_rollbackFrom = m_rollbackFrom ? std::min(*m_rollbackFrom, frame) : frame;
    }
}

PadInput Netplay::predicted(int port) const
{
    // La meilleure supposition : il appuie sur la même chose qu'à sa
    // dernière image connue.
    const Stream& s = m_streams[port];
    if (s.next == 0)
        return {};
    const auto it = s.inputs.find(s.next - 1);
    return it != s.inputs.end() ? it->second : PadInput{};
}

FrameInputs Netplay::bestInputs(uint32_t frame) const
{
    FrameInputs inputs{};
    for (size_t p = 0; p < m_players.size(); ++p) {
        const auto it = m_streams[p].inputs.find(frame);
        inputs[p] = it != m_streams[p].inputs.end() ? it->second : predicted(static_cast<int>(p));
    }
    return inputs;
}

uint32_t Netplay::minConfirmed() const
{
    uint32_t minimum = std::numeric_limits<uint32_t>::max();
    for (size_t p = 0; p < m_players.size(); ++p)
        minimum = std::min(minimum, m_streams[p].next);
    return minimum;
}

std::string* Netplay::storedState(uint32_t frame)
{
    if (m_states.empty())
        return nullptr;
    auto& slot = m_states[frame % m_states.size()];
    return slot.first == frame ? &slot.second : nullptr;
}

// --- Une image de partie ---------------------------------------------------------------------------

int Netplay::tick(Session& session, Emulator& emulator, const PadInput& local)
{
    if (m_phase != Phase::Running)
        return 0;
    ++m_ticks;
    receiveFrames(session);

    // 1. Retour en arrière : on s'était trompé sur des touches déjà jouées.
    if (m_rollbackFrom && *m_rollbackFrom < m_frame) {
        const uint32_t from = *m_rollbackFrom;
        std::string* state = storedState(from);
        if (!state || !emulator.load(*state)) {
            stop(session, "Désynchronisation : retour en arrière impossible");
            return 0;
        }
        // On recalcule, sans le son, toutes les images depuis l'erreur,
        // en rephotographiant chacune (elles ont changé).
        for (uint32_t f = from; f < m_frame; ++f) {
            if (f != from)
                snapshot(emulator, f);
            const FrameInputs inputs = bestInputs(f);
            m_used[f] = inputs;
            emulator.run(inputs, true);
        }
        ++m_rollbackCount;
        m_deepestRollback = std::max(m_deepestRollback, static_cast<int>(m_frame - from));
    }
    m_rollbackFrom.reset();

    // 2. Synchronisation de la vitesse : si on a de l'avance sur les autres,
    // on attend une image de temps en temps ; si on est en retard, on en
    // calcule deux d'un coup.
    int remote = 0;
    if (m_isHost) {
        int slowest = std::numeric_limits<int>::max();
        for (const auto& [id, c] : m_clients)
            slowest = std::min(slowest, static_cast<int>(c.frame) + std::max(0, session.pingOf(id)) / 34);
        remote = slowest == std::numeric_limits<int>::max() ? static_cast<int>(m_frame) : slowest;
    } else {
        remote = static_cast<int>(m_hostFrame) + std::max(0, session.pingOf(0)) / 34;
    }
    const int advantage = static_cast<int>(m_frame) - remote;
    int steps = 1;
    if (advantage >= 2 && m_ticks % 3 == 0)
        steps = 0;
    else if (advantage <= -3)
        steps = 2;

    // 3. Avancer.
    int advanced = 0;
    for (int i = 0; i < steps; ++i) {
        // On ne devine jamais plus de "rollback" images à l'avance (en mode
        // délai, rollback = 0 : on attend toujours les vraies touches).
        if (m_frame >= minConfirmed() + static_cast<uint32_t>(m_maxRollback))
            break;
        confirm(m_localPort, m_frame + static_cast<uint32_t>(m_delay), local);
        const FrameInputs inputs = bestInputs(m_frame);

        // Photographie avant l'image (pour pouvoir y revenir), et contrôle
        // de désynchronisation toutes les 120 images.
        const bool checkpoint = m_frame > 0 && m_frame % kCheckEvery == 0;
        if (m_maxRollback > 0 || checkpoint)
            snapshot(emulator, m_frame);
        if (checkpoint)
            m_pendingCheckpoints.insert(m_frame);

        m_used[m_frame] = inputs;
        emulator.run(inputs, false);
        // EMU_NET_TEST_DIVERGE=n (test) : un membre calcule des images de
        // trop à l'image n, pour vérifier que la resynchronisation répare.
        if (const char* diverge = SDL_getenv("EMU_NET_TEST_DIVERGE");
            diverge && !m_isHost && m_generation == 0 && m_frame == static_cast<uint32_t>(SDL_atoi(diverge))) {
            FrameInputs garbage{};
            garbage[0].buttons = 0x0FFF;
            for (int extra = 0; extra < 30; ++extra)
                emulator.run(garbage, true);
        }
        ++m_frame;
        ++advanced;
    }

    checkpoints(session);
    sendFrames(session);

    // 4. Ménage : on oublie ce qui ne servira plus.
    const uint32_t keepUsed = m_frame > static_cast<uint32_t>(m_maxRollback) + 2 ? m_frame - m_maxRollback - 2 : 0;
    m_used.erase(m_used.begin(), m_used.lower_bound(keepUsed));
    for (size_t p = 0; p < m_players.size(); ++p) {
        Stream& s = m_streams[p];
        uint32_t keep = s.next > 0 ? s.next - 1 : 0; // la dernière sert aux suppositions
        // ... et surtout pas celles des images pas encore jouées ou qu'un
        // retour en arrière pourrait recalculer ! (Bug trouvé en test : avec
        // un ping nul, les touches arrivent EN AVANCE et étaient effacées
        // avant d'avoir servi, d'où une désynchronisation.)
        keep = std::min(keep, keepUsed);
        if (m_isHost)
            for (const auto& [id, c] : m_clients)
                keep = std::min(keep, c.haveNext[p]); // pas encore envoyée à ce membre
        else if (static_cast<int>(p) == m_localPort)
            keep = std::min(keep, m_hostHasMine); // pas encore reçue par l'hôte
        s.inputs.erase(s.inputs.begin(), s.inputs.lower_bound(keep));
    }

    if (advanced > 0)
        m_stalledSince = 0;
    else if (!m_stalledSince)
        m_stalledSince = SDL_GetTicks();
    return advanced;
}

void Netplay::snapshot(Emulator& emulator, uint32_t f)
{
    auto& slot = m_states[f % m_states.size()];
    if (emulator.save(slot.second))
        slot.first = f;
    // Aux images de contrôle, on note aussi l'empreinte de la mémoire du jeu
    // (au même moment que la photographie : avant l'image f).
    if (f > 0 && f % kCheckEvery == 0 && emulator.fingerprint)
        m_fingerprints[f] = emulator.fingerprint();
}

void Netplay::checkpoints(Session& session)
{
    // L'état photographié avant l'image c est "définitif" quand toutes les
    // touches avant c sont confirmées : on peut alors comparer les empreintes.
    for (auto it = m_pendingCheckpoints.begin(); it != m_pendingCheckpoints.end();) {
        const uint32_t c = *it;
        if (minConfirmed() < c) {
            ++it;
            continue;
        }
        // De préférence l'empreinte de la mémoire du jeu : celle de toute la
        // sauvegarde d'état contient, chez certains cores (mGBA, gpSP), des
        // compteurs de son qui diffèrent d'un PC à l'autre alors que le jeu,
        // lui, est identique. D'où de fausses alertes.
        uint64_t hash = 0;
        if (const auto fp = m_fingerprints.find(c); fp != m_fingerprints.end()) {
            hash = fp->second;
            m_fingerprints.erase(fp);
        }
        if (const std::string* state = storedState(c)) {
            if (!hash)
                hash = Hasher64::of(*state);
            // EMU_NET_DUMP=préfixe : écrit chaque état contrôlé, pour comparer
            // octet par octet ceux de deux joueurs après une désynchronisation.
            if (const char* dump = SDL_getenv("EMU_NET_DUMP"))
                std::ofstream(std::string(dump) + (m_isHost ? "-hote-" : "-invite-") + std::to_string(c) + ".bin",
                              std::ios::binary)
                    .write(state->data(), static_cast<std::streamsize>(state->size()));
        }
        if (hash) {
            if (m_isHost)
                checkHash(session, c, 0, hash);
            else
                session.sendApp(-1, ByteWriter().u8(kCheck).u8(m_generation).u32(c).u64(hash).data());
        }
        it = m_pendingCheckpoints.erase(it);
        if (m_phase != Phase::Running)
            break; // resynchronisation lancée
    }
    // Les empreintes d'images trop anciennes (jamais contrôlées) ne servent plus.
    while (!m_fingerprints.empty() && m_fingerprints.begin()->first + 4 * kCheckEvery < m_frame)
        m_fingerprints.erase(m_fingerprints.begin());
}

// --- Réseau -----------------------------------------------------------------------------------------

void Netplay::receiveFrames(Session& session)
{
    for (const Session::FramePacket& packet : session.takeFrames()) {
        ByteReader r(packet.data);
        if (r.u8() != m_generation)
            continue; // reste d'avant une resynchronisation
        if (m_isHost) {
            // Un membre envoie : où il en est, ce qu'il a déjà, et ses touches.
            const auto client = m_clients.find(packet.from);
            const int port = portOf(packet.from);
            if (client == m_clients.end() || port < 0)
                continue;
            client->second.frame = std::max(client->second.frame, r.u32());
            const uint8_t ports = std::min<uint8_t>(r.u8(), kNetPlayers);
            for (int p = 0; p < ports; ++p)
                client->second.haveNext[p] = std::max(client->second.haveNext[p], r.u32());
            const uint32_t first = r.u32();
            const uint8_t count = r.u8();
            for (uint32_t i = 0; i < count && r.ok(); ++i) {
                const PadInput in = readInput(r);
                if (r.ok())
                    confirm(port, first + i, in);
            }
        } else {
            // L'hôte envoie : où il en est, ce qu'il a de moi, et les touches des autres.
            m_hostFrame = std::max(m_hostFrame, r.u32());
            m_hostHasMine = std::max(m_hostHasMine, r.u32());
            const uint8_t streams = r.u8();
            for (int s = 0; s < streams && r.ok(); ++s) {
                const uint8_t port = r.u8();
                const uint32_t first = r.u32();
                const uint8_t count = r.u8();
                for (uint32_t i = 0; i < count && r.ok(); ++i) {
                    const PadInput in = readInput(r);
                    if (r.ok() && port != m_localPort)
                        confirm(port, first + i, in);
                }
            }
        }
    }
}

void Netplay::sendFrames(Session& session)
{
    // Chaque paquet reprend TOUT ce que l'autre n'a pas encore confirmé :
    // un paquet perdu est rattrapé par le suivant, sans renvoi.
    auto appendRun = [](ByteWriter& w, const Stream& s, uint32_t first) {
        uint32_t count = 0;
        while (count < kMaxPerPacket && s.inputs.count(first + count))
            ++count;
        w.u32(first).u8(static_cast<uint8_t>(count));
        for (uint32_t i = 0; i < count; ++i)
            writeInput(w, s.inputs.at(first + i));
    };

    if (m_isHost) {
        for (auto& [id, client] : m_clients) {
            const int clientPort = portOf(id);
            ByteWriter w;
            w.u8(m_generation).u32(m_frame).u32(m_streams[clientPort].next).u8(static_cast<uint8_t>(m_players.size() - 1));
            for (size_t p = 0; p < m_players.size(); ++p) {
                if (static_cast<int>(p) == clientPort)
                    continue;
                w.u8(static_cast<uint8_t>(p));
                appendRun(w, m_streams[p], std::max(client.haveNext[p], m_streams[p].inputs.empty() ? 0u : m_streams[p].inputs.begin()->first));
            }
            sendFrame(session, id, w.data());
        }
    } else {
        ByteWriter w;
        w.u8(m_generation).u32(m_frame).u8(kNetPlayers);
        for (const Stream& s : m_streams)
            w.u32(s.next);
        const Stream& mine = m_streams[m_localPort];
        appendRun(w, mine, std::max(m_hostHasMine, mine.inputs.empty() ? 0u : mine.inputs.begin()->first));
        sendFrame(session, -1, w.data());
    }

    // Latence simulée : on n'envoie qu'à l'heure prévue.
    const Uint64 now = SDL_GetTicks();
    for (auto it = m_delayed.begin(); it != m_delayed.end();) {
        if (it->sendAt <= now) {
            session.sendFrame(it->to, it->data);
            it = m_delayed.erase(it);
        } else {
            ++it;
        }
    }
}

void Netplay::sendFrame(Session& session, int to, const std::string& data)
{
    // Tests : paquet "perdu en route" (le suivant le rattrapera).
    if (m_simulatedLossPct > 0 && SDL_rand(100) < m_simulatedLossPct)
        return;
    if (m_simulatedLatencyMs <= 0 && m_simulatedJitterMs <= 0) {
        session.sendFrame(to, data);
        return;
    }
    // Le ping est un aller-retour : chaque côté retarde de la moitié, plus ou
    // moins l'irrégularité (les paquets peuvent alors arriver dans le désordre).
    const int jitter = m_simulatedJitterMs > 0 ? SDL_rand(2 * m_simulatedJitterMs + 1) - m_simulatedJitterMs : 0;
    const int delay = std::max(0, m_simulatedLatencyMs / 2 + jitter);
    m_delayed.push_back({SDL_GetTicks() + static_cast<Uint64>(delay), to, data});
}

void Netplay::checkHash(Session& session, uint32_t frame, uint8_t member, uint64_t hash)
{
    if (member == 0) {
        m_hashes[frame] = hash;
        auto waiting = m_pendingChecks.extract(frame);
        if (!waiting.empty())
            for (const auto& [id, h] : waiting.mapped())
                checkHash(session, frame, id, h);
        return;
    }
    const auto own = m_hashes.find(frame);
    if (own == m_hashes.end()) {
        m_pendingChecks[frame].push_back({member, hash}); // l'hôte n'y est pas encore
        return;
    }
    if (own->second != hash) {
        // Les parties ont divergé : sans rien faire, chacun continuerait dans
        // "son" jeu (positions, circuit... différents). On répare.
        ++m_desyncs;
        SDL_Log("Désynchronisation avec %s à l'image %u : resynchronisation", nameOf(member).c_str(), frame);
        resync();
    }
}

void Netplay::resync()
{
    if (!m_isHost || m_phase != Phase::Running)
        return;
    // Comme au lancement : l'hôte photographie SA partie (App::updateNetplay,
    // via hostNeedsState), l'envoie avec un nouveau numéro de manche, chacun
    // la recharge, puis "C'est parti" repart de l'image 0. Une pause d'une
    // seconde environ, au lieu de deux parties différentes.
    ++m_generation;
    ++m_resyncs;
    m_applied.clear();
    m_phase = Phase::Syncing;
    m_hostNeedsState = true;
    m_messages.push_back("Vos parties avaient divergé : resynchronisation…");
}

// --- Fin de partie ---------------------------------------------------------------------------

void Netplay::stop(Session& session, const std::string& reason)
{
    if (!active())
        return;
    session.sendApp(-1, ByteWriter().u8(kStop).str(reason).data());
    finish(reason);
}

void Netplay::finish(const std::string& reason)
{
    if (!active())
        return;
    m_phase = Phase::Idle;
    m_hostNeedsState = false;
    m_stopped = true;
    m_used.clear();
    m_states.clear();
    m_delayed.clear();
    for (Stream& s : m_streams)
        s = {};
    if (!reason.empty())
        m_messages.push_back(reason);
}

std::vector<std::string> Netplay::takeMessages()
{
    std::vector<std::string> list;
    list.swap(m_messages);
    return list;
}

bool Netplay::takeStopped()
{
    const bool stopped = m_stopped;
    m_stopped = false;
    return stopped;
}

// --- Informations ------------------------------------------------------------------------------

int Netplay::portOf(uint8_t memberId) const
{
    for (const Player& p : m_players)
        if (p.memberId == memberId)
            return p.port;
    return -1;
}

std::string Netplay::nameOf(uint8_t memberId) const
{
    for (const Player& p : m_players)
        if (p.memberId == memberId)
            return p.name;
    return "un joueur";
}

std::string Netplay::statusText() const
{
    switch (m_phase) {
    case Phase::Idle: return {};
    case Phase::Loading: return "Chargement du jeu chez tous les joueurs…";
    case Phase::Syncing:
        if (m_isHost)
            return "Envoi de la partie aux joueurs…";
        if (!m_state.empty())
            return "Réception de la partie… " + std::to_string(m_stateReceived * 100 / m_state.size()) + " %";
        return "En attente de l'hôte…";
    case Phase::Running: {
        // On ne prévient qu'après une demi-seconde d'attente (sinon ça clignote).
        if (!m_stalledSince || SDL_GetTicks() - m_stalledSince < 500)
            return {};
        const uint32_t lowest = minConfirmed();
        std::string names;
        for (const Player& p : m_players)
            if (static_cast<int>(p.port) != m_localPort && m_streams[p.port].next == lowest)
                names += (names.empty() ? "" : ", ") + p.name;
        return "En attente de " + (names.empty() ? std::string("la connexion") : names) + "…";
    }
    }
    return {};
}

} // namespace net
