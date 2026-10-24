#pragma once
// SpeedrunCom : les catégories officielles d'un jeu et leur record du monde,
// lus sur speedrun.com (son API publique, gratuite et sans compte).
//
// La recherche se fait dans un thread (plusieurs requêtes web) : l'écran
// Speedrun lance search(), puis récupère le résultat avec take().

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class SpeedrunCom {
public:
    struct OfficialCategory {
        std::string name;    // "Any%", "120 Star"...
        std::string weblink; // la page du classement
        long long wrMs = -1; // record du monde, -1 si aucun
        std::string wrBy;    // son auteur
    };
    struct Result {
        std::string error;   // vide si tout va bien
        std::string gameName;
        std::string gameLink;
        std::vector<OfficialCategory> categories;
    };

    SpeedrunCom() = default;
    ~SpeedrunCom();
    SpeedrunCom(const SpeedrunCom&) = delete;
    SpeedrunCom& operator=(const SpeedrunCom&) = delete;

    void search(const std::string& gameTitle);
    bool busy() const { return m_busy; }
    std::optional<Result> take();

private:
    void run(std::string title);

    std::thread m_thread;
    std::atomic<bool> m_busy{false};
    std::mutex m_mutex;
    std::optional<Result> m_result;
};
