#pragma once
// Scraper : trouve tout seul la jaquette et les informations de chaque jeu.
//
// Les sources sont libres et ne demandent aucun compte :
//   - libretro-database : nom officiel, genre, développeur, éditeur, année,
//     nombre de joueurs (un fichier .rdb par console, téléchargé une fois) ;
//   - thumbnails.libretro.com : jaquettes (boxarts), captures d'écran et
//     écrans-titres, rangés par nom officiel.
//
// Pour reconnaître un jeu :
//   1. on calcule le CRC32 du fichier (cartouches seulement : un ISO de PS2
//      serait trop long à lire) et on le cherche dans la base -> certain ;
//   2. sinon on compare le nom du fichier aux noms de la base -> probable.
//
// Tout se passe dans un thread : l'interface ne se fige jamais. Les
// résultats sont gardés dans scraper/games.txt et les images dans
// scraper/images/ : on ne télécharge chaque chose qu'une fois.

#include "../Cheats.hpp"
#include "../Library.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace scrape {

struct GameInfo {
    enum class Status { Found, NotFound };
    Status status = Status::NotFound;
    bool byCrc = false;      // reconnu par l'empreinte du fichier (sûr) ou par son nom
    std::string name;        // nom officiel : "Street Fighter Alpha 2 (USA)"
    std::string genre;
    std::string developer;
    std::string publisher;
    int year = 0;
    int players = 0;
    // Images téléchargées (chemins complets en UTF-8, vides si absentes).
    std::string boxart;
    std::string snap;        // capture d'écran en jeu
    std::string titleScreen; // écran-titre
    long long time = 0;      // date de la recherche
    std::string database;    // base libretro où il a été reconnu ("MAME"...)
};

// Une jaquette proposée dans la fenêtre "Changer la jaquette".
struct CoverChoice {
    std::string name; // "Street Fighter Alpha 2 (Europe)"
    std::string file; // image téléchargée
};

class Scraper {
public:
    // dir : dossier du scraper (bases, listes et images en cache).
    explicit Scraper(std::filesystem::path dir);
    ~Scraper();
    Scraper(const Scraper&) = delete;
    Scraper& operator=(const Scraper&) = delete;

    // Ce que l'on sait d'un jeu (nullptr = jamais cherché).
    const GameInfo* find(const std::filesystem::path& game) const;

    // Chercher un jeu. force : même s'il a déjà été cherché.
    void request(const Game& game, bool force = false);
    // Chercher tous les jeux jamais cherchés.
    void requestMissing(const std::vector<Game>& games);
    bool queued(const std::filesystem::path& game) const;
    int pending() const;            // jeux en attente (en cours compris)
    std::string current() const;    // titre du jeu en cours de recherche

    // À appeler à chaque image : intègre les résultats du thread.
    // Renvoie true si quelque chose a changé (pour rafraîchir l'affichage).
    bool update();
    std::vector<std::string> takeMessages(); // erreurs à afficher

    // --- Fenêtre "Changer la jaquette" ------------------------------------
    // Cherche les jaquettes dont le nom contient tous les mots de query.
    void searchCovers(const Game& game, const std::string& query);
    const std::vector<CoverChoice>& coverChoices() const { return m_choices; }
    bool coverSearchBusy() const { return m_searchBusy; }

    // --- Codes de triche tout prêts (base libretro, fichiers .cht) ---------
    void searchCheats(const Game& game);
    const std::vector<Cheat>& onlineCheats() const { return m_cheats; }
    bool cheatSearchBusy() const { return m_cheatBusy; }
    const std::string& cheatSearchNote() const { return m_cheatNote; } // d'où ils viennent, ou pourquoi rien

    void forget(const Game& game);
    const std::filesystem::path& dir() const { return m_dir; }

private:
    struct Job {
        enum class Kind { Game, CoverSearch, CheatSearch } kind = Kind::Game;
        std::string key;               // UserData::keyOf(path)
        std::filesystem::path path;
        std::string systemId;
        std::string fileTitle;         // titre tiré du nom du fichier
        std::string title;             // titre affiché (peut avoir été renommé)
        std::string region;
        std::uintmax_t size = 0;
        std::string query;             // recherche de jaquettes
        int searchId = 0;
    };
    struct Result {
        enum class Kind { Game, Cover, SearchDone, Message, Cheats } kind = Kind::Game;
        std::string key;
        GameInfo info;
        CoverChoice choice;
        int searchId = 0;
        std::string message;
        std::vector<Cheat> cheats;
    };

    void worker();
    GameInfo scrapeGame(const Job& job);
    void searchCoversJob(const Job& job);
    void searchCheatsJob(const Job& job);
    void post(Result result);
    void load();
    void save() const;

    std::filesystem::path m_dir;
    std::map<std::string, GameInfo> m_infos; // clé : UserData::keyOf(jeu) — thread principal seulement

    // Partagé avec le thread : protégé par m_mutex.
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_queue;
    std::string m_current;
    bool m_working = false;
    std::vector<Result> m_results;
    std::atomic<bool> m_stop{false};

    // Recherche de jaquettes : seuls les résultats de la dernière comptent.
    int m_searchId = 0;
    bool m_searchBusy = false;
    std::vector<CoverChoice> m_choices;
    std::vector<std::string> m_messages;
    bool m_cheatBusy = false;
    std::vector<Cheat> m_cheats;
    std::string m_cheatNote;

    // [C++] "struct Cache;" déclaré ici, défini dans le .cpp : les détails
    // (bases chargées, listes d'images, client HTTP) n'encombrent pas cet
    // en-tête. Le thread est le seul à s'en servir.
    struct Cache;
    std::unique_ptr<Cache> m_cache;

    std::thread m_thread; // en dernier : démarré quand tout le reste existe
};

} // namespace scrape
