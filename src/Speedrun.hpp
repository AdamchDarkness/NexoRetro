#pragma once
// Speedrun : le chronomètre des speedrunners, à la manière de LiveSplit.
//
// Pour chaque jeu, des CATÉGORIES ("Any%", "100%"...), chacune découpée en
// SPLITS (les étapes de la run : "Monde 1", "Bowser"...). On retient :
//  - le record personnel (PB) : le temps de passage à chaque split pendant
//    la meilleure run terminée ;
//  - le meilleur temps de chaque segment ("gold"), même pris dans des runs
//    abandonnées : leur somme dit le temps idéal possible ("somme des meilleurs") ;
//  - le nombre d'essais, de runs terminées, et l'historique des runs.
//
// Le chrono compte le temps réel (RTA), comme sur speedrun.com. Une run où
// l'on a chargé un état, rembobiné ou accéléré est notée "assistée" : elle
// reste dans l'historique, mais ne peut pas devenir un record.
//
// Tout est enregistré dans <données>/speedrun.txt (lisible au Bloc-notes).

#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace speedrun {

struct Segment {
    std::string name;
    long long pbSplit = -1;     // temps de passage dans le PB (ms depuis le départ), -1 = aucun
    long long bestSegment = -1; // meilleure durée de ce segment seul (ms), -1 = aucune
};

struct Run {
    long long date = 0;          // départ (secondes depuis 1970)
    long long timeMs = -1;       // temps final ; -1 = run abandonnée (remise à zéro)
    int splitsDone = 0;
    bool assisted = false;
    std::string video;           // fichier vidéo de la run (vide = pas d'enregistrement)
    std::vector<long long> splits; // temps de passage, -1 = split passé (sauté)
};

struct Category {
    std::string name = "Any%";
    std::vector<Segment> segments; // au moins un : le dernier split termine la run
    int attempts = 0;
    int finished = 0;
    std::vector<Run> history;      // les plus anciennes d'abord
    // Catégorie officielle de speedrun.com (vide sinon) : son record du monde.
    long long worldRecord = -1;
    std::string worldRecordBy;
    std::string officialLink;      // la page du classement

    long long pb() const { return segments.empty() ? -1 : segments.back().pbSplit; }
    // Somme des meilleurs segments (-1 si l'un d'eux n'a jamais été fait).
    long long sumOfBest() const;
};

struct GameRuns {
    std::vector<Category> categories;
    int current = 0; // catégorie choisie
    Category& category();
};

// "1:23.45", "12:03:21.7"... (temps négatif : avec un signe, pour les écarts).
std::string formatTime(long long ms, bool showSign = false, int decimals = 2);

class Speedrun {
public:
    enum class State { Ready, Running, Finished };

    explicit Speedrun(std::filesystem::path file);
    void load();
    void save() const;

    // --- Les données ------------------------------------------------------
    // Les runs d'un jeu (créées vides, avec une catégorie "Any%", au besoin).
    GameRuns& game(const std::filesystem::path& gamePath);
    const GameRuns* find(const std::filesystem::path& gamePath) const;
    // Les jeux qui ont des runs (clés = chemins complets, UTF-8).
    const std::map<std::string, GameRuns>& games() const { return m_games; }
    void forgetGame(const std::filesystem::path& gamePath);

    // --- La run en cours --------------------------------------------------
    // Le jeu lancé (vide = aucun) : le chrono se rattache à sa catégorie choisie.
    void setGame(const std::filesystem::path& gamePath, std::string title);
    const std::filesystem::path& currentGame() const { return m_gamePath; }
    const std::string& currentTitle() const { return m_title; }
    // Mode speedrun : chrono affiché en jeu. S'active au premier départ.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    void startOrSplit(); // départ, ou passage du split en cours
    void undoSplit();    // revenir au split précédent (passé trop tôt)
    void skipSplit();    // passer un split sans temps
    void reset();        // remettre le chrono à zéro (la run est enregistrée)
    void markAssisted(); // état chargé, retour arrière, avance rapide...

    State state() const { return m_state; }
    bool assisted() const { return m_assisted; }
    long long elapsedMs() const;
    int currentSplit() const { return m_split; }
    const std::vector<long long>& splits() const { return m_splits; }
    // Les splits de cette run qui ont battu leur meilleur segment ("gold").
    const std::vector<char>& golds() const { return m_golds; }
    // Le record auquel cette run se compare : celui d'AVANT son départ (s'il
    // est battu à l'arrivée, les écarts restent lisibles). -1 = pas de temps.
    long long comparison(int split) const;
    // La catégorie de la run (nullptr sans jeu).
    Category* category();
    // Nouveau record à la fin de la dernière run ?
    bool lastWasPb() const { return m_lastWasPb; }

    // Événements pour l'enregistrement vidéo : une run vient de partir /
    // vient de finir (terminée ou remise à zéro). take* : une seule fois.
    bool takeStarted();
    bool takeEnded();
    // La vidéo de la dernière run enregistrée (à rattacher à son historique).
    void attachVideo(const std::string& file);
    // La dernière run de l'historique (celle qui vient de finir).
    const Run* lastRun();

private:
    void finishRun(bool completed);

    std::filesystem::path m_file;
    std::filesystem::path m_base; // dossier des données (chemins relatifs, clé USB)
    std::map<std::string, GameRuns> m_games;

    std::filesystem::path m_gamePath;
    std::string m_gameKey;
    std::string m_title;
    bool m_enabled = false;
    State m_state = State::Ready;
    Uint64 m_startNs = 0;
    Uint64 m_endNs = 0;
    long long m_startDate = 0;
    int m_split = 0;
    std::vector<long long> m_splits;
    std::vector<char> m_golds;
    std::vector<long long> m_compare;
    bool m_assisted = false;
    bool m_lastWasPb = false;
    bool m_started = false;
    bool m_ended = false;
};

} // namespace speedrun

namespace speedrun {

// --- LiveSplit (.lss) --------------------------------------------------------
// Le format de LiveSplit, le chrono le plus utilisé par les speedrunners (un
// fichier XML). On en lit les splits, le record (temps de passage
// "Personal Best"), les meilleurs segments et le nombre d'essais.
bool importLss(const std::filesystem::path& file, Category& out, std::string& gameName, std::string& error);
bool exportLss(const std::filesystem::path& file, const Category& category, const std::string& gameName);

} // namespace speedrun
