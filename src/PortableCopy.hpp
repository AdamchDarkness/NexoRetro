#pragma once
// PortableCopy : fabriquer une copie portable complète de Nexo Retro (sur
// une clé USB, un disque externe...), utilisable sur n'importe quel PC sans
// rien installer.
//
// Dans <destination>\NexoRetro\ :
//   NexoRetro.exe, assets\        le programme
//   portable.txt                  "les données sont à côté de l'exe"
//   cores\ bios\ roms\ saves\ ... les données (réglages, jaquettes, temps de jeu...)
// Les jeux rangés ailleurs (dossiers surveillés, fichiers ajoutés à la
// main) sont recopiés dans roms\<console>\ : la clé contient TOUT. Les
// chemins y sont notés par rapport au dossier (voir PortablePath.hpp) :
// la lettre de la clé peut changer d'un PC à l'autre.
//
// La copie peut durer longtemps (des Go de jeux) : elle se fait dans un
// thread, l'interface suit sa progression.

#include "UserData.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class PortableCopy {
public:
    struct Plan {
        std::filesystem::path exeDir;  // dossier du programme
        std::filesystem::path dataDir; // dossier des données
        std::filesystem::path dest;    // dossier choisi (la copie va dans dest\NexoRetro)
        // Jeux hors du dossier des données : fichier -> console (roms\<console>\).
        std::vector<std::pair<std::filesystem::path, std::string>> outsideGames;
        UserData userData{{}};          // instantané des données utilisateur
    };

    PortableCopy() = default;
    ~PortableCopy();
    PortableCopy(const PortableCopy&) = delete;
    PortableCopy& operator=(const PortableCopy&) = delete;

    void start(Plan plan);
    bool busy() const { return m_busy; }
    float progress() const { return m_progress; } // 0 à 1
    // Le message de fin, une seule fois (vide tant que ce n'est pas fini).
    std::optional<std::string> takeResult();

private:
    void run(Plan plan);

    std::thread m_thread;
    std::atomic<bool> m_busy{false};
    std::atomic<float> m_progress{0.0f};
    std::mutex m_mutex;
    std::optional<std::string> m_result;
};
