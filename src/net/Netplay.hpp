#pragma once
// Netplay : jouer à plusieurs à travers Internet, avec "rollback".
//
// Tous les émulateurs font tourner la même partie, image par image, avec
// les mêmes touches : ils calculent donc exactement la même chose. On
// n'échange que les touches (quelques octets par image).
//
// Le problème : les touches de l'ami mettent du temps à arriver. Deux
// façons de faire :
//
//  - "Délai" (lockstep) : on attend d'avoir les touches de tout le monde
//    avant de calculer une image. Mes touches sont appliquées quelques
//    images plus tard, le temps qu'elles voyagent. Simple, mais on sent le
//    retard (5 images = 83 ms avec 60 ms de ping).
//
//  - "Rollback" (comme GGPO, dans les jeux de combat) : on n'attend pas.
//    On DEVINE les touches de l'ami (il appuie sûrement sur la même chose
//    qu'à l'image d'avant) et on avance. Avant chaque image, on photographie
//    l'état de l'émulateur. Quand ses vraies touches arrivent et ne
//    correspondent pas, on recharge la photographie de cette image-là et
//    on recalcule jusqu'à maintenant, en une fraction de seconde. Mes
//    touches à moi s'appliquent (presque) tout de suite.
//
// Le mode "délai" est simplement un rollback de 0 image : on ne devine
// jamais, on attend. Un seul algorithme gère les deux.
//
// Déroulement d'une partie :
//   1. 'S' Lancement : l'hôte envoie la liste des joueurs.
//   2. 'L' Chargement : chacun démarre le jeu et répond avec son émulateur.
//   3. 'D' Synchronisation : l'hôte envoie la photographie de son émulateur
//      en morceaux ; chacun la charge ('R') : tout le monde part du même état.
//   4. 'G' Partie : l'hôte donne le départ, avec le délai et la profondeur
//      de rollback. Chaque joueur envoie ses touches à l'hôte, qui relaie
//      celles de chacun à tous les autres (topologie en étoile).

#include "Session.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace net {

// Les touches d'un joueur pour une image.
struct PadInput {
    uint16_t buttons = 0;     // 1 bit par bouton du RetroPad
    int16_t analog[4] = {};   // stick gauche X/Y, stick droit X/Y
    bool operator==(const PadInput&) const = default;
};

constexpr int kNetPlayers = 4; // ports de manette d'une console
using FrameInputs = std::array<PadInput, kNetPlayers>;

// Ce que le netplay demande à l'émulateur. App fournit ces trois fonctions.
struct Emulator {
    std::function<bool(std::string&)> save;        // photographier l'état
    std::function<bool(const std::string&)> load;  // recharger une photographie
    // Calculer une image avec ces touches. silent : sans le son (recalcul).
    std::function<void(const FrameInputs&, bool silent)> run;
    // Empreinte de la mémoire du jeu, pour vérifier que tout le monde voit la
    // même partie (0 : inconnue, on compare alors les sauvegardes d'état).
    std::function<uint64_t()> fingerprint;
};

class Netplay {
public:
    enum class Phase { Idle, Loading, Syncing, Running };

    struct Player {
        uint8_t memberId = 0;
        uint8_t port = 0; // 0 = joueur 1
        std::string name;
    };

    // --- Hôte : démarrer une partie avec tous les membres du salon -------
    bool hostStart(Session& session, std::string& error);

    // App transmet chaque message de jeu reçu, et appelle update() à chaque image.
    void onAppMessage(Session& session, uint8_t from, const std::string& data);
    void update(Session& session);
    void onMemberLeft(Session& session, const std::string& reason);
    void onSalonClosed(const std::string& reason);

    // --- Chargement : App démarre le jeu, puis rend compte ---------------
    bool wantsLaunch() const { return m_phase == Phase::Loading && !m_launched; }
    // rollbackSafe : les sauvegardes d'état du core sont fiables (sinon, mode délai).
    void launched(Session& session, bool ok, const std::string& coreInfo, bool rollbackSafe, const std::string& error);

    // --- Synchronisation de l'état ------------------------------------------
    bool hostNeedsState() const { return m_hostNeedsState; }
    void hostSendState(Session& session, const std::string& state);
    std::optional<std::string> takeReceivedState();
    void stateApplied(Session& session);

    // --- Partie ----------------------------------------------------------------
    bool running() const { return m_phase == Phase::Running; }
    // Avance la partie (recalculs compris). local : mes touches de maintenant.
    // Renvoie le nombre d'images avancées (0 si on attend les autres).
    int tick(Session& session, Emulator& emulator, const PadInput& local);

    // --- Informations ------------------------------------------------------------
    bool active() const { return m_phase != Phase::Idle; }
    Phase phase() const { return m_phase; }
    uint32_t frame() const { return m_frame; }
    int delay() const { return m_delay; }
    int rollbackFrames() const { return m_maxRollback; }
    int localPort() const { return m_localPort; }
    const std::vector<Player>& players() const { return m_players; }
    int desyncs() const { return m_desyncs; }
    int resyncs() const { return m_resyncs; }
    int rollbacks() const { return m_rollbackCount; }
    int deepestRollback() const { return m_deepestRollback; }
    std::string statusText() const; // "En attente de Lou…", pour l'écran de jeu

    void stop(Session& session, const std::string& reason);
    std::vector<std::string> takeMessages();
    bool takeStopped();

    // Test sur un seul PC : retarde l'envoi des touches pour simuler un ping.
    void setSimulatedLatency(int ms) { m_simulatedLatencyMs = ms; }
    // Hôte : rollback aussi pour les consoles aux états très lourds (PS2,
    // GameCube), grâce aux photos différentielles. Sinon, mode délai pour elles.
    void setHeavyRollback(bool on) { m_heavyRollback = on; }
    // ... et un Internet imparfait : paquets de touches perdus (en %) et
    // ping irrégulier (± ms).
    void setSimulatedLoss(int percent, int jitterMs)
    {
        m_simulatedLossPct = percent;
        m_simulatedJitterMs = jitterMs;
    }

private:
    // Les touches CONFIRMÉES d'un joueur (reçues, pas devinées).
    struct Stream {
        std::map<uint32_t, PadInput> inputs;
        uint32_t next = 0; // toutes les images avant celle-ci sont connues
    };
    struct ClientSync {
        std::array<uint32_t, kNetPlayers> haveNext{}; // ce qu'il a déjà, joueur par joueur
        uint32_t frame = 0;                            // où en est sa partie
    };
    struct Delayed {
        Uint64 sendAt;
        int to;
        std::string data;
    };

    void startRunning(uint8_t delay, uint8_t maxRollback);
    void finish(const std::string& reason);
    // Ajoute une touche confirmée ; si on avait mal deviné, prévoit un recalcul.
    void confirm(int port, uint32_t frame, const PadInput& input);
    PadInput predicted(int port) const;
    FrameInputs bestInputs(uint32_t frame) const;
    uint32_t minConfirmed() const;
    void receiveFrames(Session& session);
    void sendFrames(Session& session);
    void sendFrame(Session& session, int to, const std::string& data);
    void checkpoints(Session& session);
    void checkHash(Session& session, uint32_t frame, uint8_t member, uint64_t hash);
    // Hôte : les parties ont divergé, tout le monde repart de la sienne.
    void resync();
    // Photographie l'état avant l'image f (et l'empreinte, si c'est un contrôle).
    void snapshot(Emulator& emulator, uint32_t f);
    // La photographie d'avant l'image "frame" (false si elle n'est plus gardée).
    bool restoreState(uint32_t frame, std::string& out) const;
    int portOf(uint8_t memberId) const;
    std::string nameOf(uint8_t memberId) const;

    Phase m_phase = Phase::Idle;
    bool m_isHost = false;
    std::vector<Player> m_players;
    int m_delay = 2;
    int m_maxRollback = 8;
    int m_localPort = 0;
    bool m_launched = false;
    std::string m_localCore;
    bool m_rollbackSafe = true;

    // Hôte : chargement et synchronisation.
    std::map<uint8_t, std::string> m_loaded; // membre -> émulateur utilisé
    bool m_hostNeedsState = false;
    size_t m_stateSize = 0;
    std::set<uint8_t> m_applied;
    // Membre : la photographie reçue en morceaux.
    std::string m_state;
    std::vector<bool> m_stateChunks;
    size_t m_stateReceived = 0;
    bool m_stateComplete = false;

    // Partie.
    uint32_t m_frame = 0;                         // prochaine image à calculer
    std::array<Stream, kNetPlayers> m_streams;
    std::map<uint32_t, FrameInputs> m_used;       // touches utilisées pour chaque image calculée
    // Les photographies, en anneau. Pour les consoles aux états très lourds
    // (PS2 : des dizaines de Mo), chaque photo n'est gardée que comme la
    // DIFFÉRENCE avec une photo "clé" partagée, refaite toutes les 30 images
    // (voir StateDelta.hpp) : quelques Ko par image au lieu de 40 Mo.
    struct StoredState {
        uint32_t frame = std::numeric_limits<uint32_t>::max();
        std::string data;                       // photo entière, ou différence avec "key"
        std::shared_ptr<const std::string> key; // photo clé (mode différentiel)
    };
    std::vector<StoredState> m_states;
    bool m_diffStates = false;                  // mode différentiel (état trop lourd)
    std::shared_ptr<const std::string> m_key;
    uint32_t m_keyFrame = 0;
    std::string m_scratch;
    bool m_heavyRollback = false;               // (hôte) rollback même pour les états lourds
    std::optional<uint32_t> m_rollbackFrom;       // première image à recalculer
    std::set<uint32_t> m_pendingCheckpoints;
    std::map<uint32_t, uint64_t> m_fingerprints;  // empreinte de la mémoire aux images de contrôle
    // Numéro de "manche" : il change à chaque resynchronisation. Les messages
    // d'une manche précédente (encore en route) sont ignorés : leurs numéros
    // d'image ne veulent plus rien dire, puisque le compte repart de 0.
    uint8_t m_generation = 0;
    // Hôte :
    std::map<uint8_t, ClientSync> m_clients;
    std::map<uint32_t, uint64_t> m_hashes;
    std::map<uint32_t, std::vector<std::pair<uint8_t, uint64_t>>> m_pendingChecks;
    // Membre :
    uint32_t m_hostHasMine = 0; // l'hôte a mes touches jusqu'à cette image
    uint32_t m_hostFrame = 0;
    uint64_t m_ticks = 0;
    Uint64 m_stalledSince = 0;

    int m_simulatedLatencyMs = 0;
    int m_simulatedLossPct = 0;
    int m_simulatedJitterMs = 0;
    std::vector<Delayed> m_delayed;

    int m_desyncs = 0;
    int m_resyncs = 0;
    int m_rollbackCount = 0;
    int m_deepestRollback = 0;
    bool m_stopped = false;
    std::vector<std::string> m_messages;
};

} // namespace net
