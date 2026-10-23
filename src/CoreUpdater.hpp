#pragma once
// CoreUpdater : garde les cores installés à jour, tout seul.
//
// Le serveur de libretro publie la liste de ses cores avec, pour chacun, la
// date de compilation et une empreinte (CRC32) de son .zip :
//
//   2026-10-22 b66d462e mgba_libretro.dll.zip
//
// On retient l'empreinte de chaque core installé (cores/versions.txt) : si
// celle du serveur est différente, une version plus récente existe. Les
// mises à jour passent ensuite une par une par l'ExternalInstaller. Le core
// du jeu en cours n'est jamais remplacé pendant la partie (Windows bloque
// d'ailleurs le fichier) : il attend la fin du jeu.

#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

class ExternalInstaller;

class CoreUpdater {
public:
    explicit CoreUpdater(std::filesystem::path coresDir);
    ~CoreUpdater();
    CoreUpdater(const CoreUpdater&) = delete;
    CoreUpdater& operator=(const CoreUpdater&) = delete;

    // Demande la liste au serveur (en arrière-plan), puis met à jour ce qui
    // doit l'être. force = false : au plus une fois toutes les 20 heures.
    void check(bool force);
    // À chaque image. coreInUse : le .dll du jeu en cours (vide sinon).
    void update(ExternalInstaller& installer, const std::string& coreInUse);
    // Une installation de core vient de se terminer (de nous ou d'ailleurs).
    // Renvoie true si c'était l'une de nos mises à jour automatiques.
    bool onInstalled(const std::string& coreFile, bool ok);

    bool checking() const { return m_fetching; }
    int queued() const { return static_cast<int>(m_queue.size() + m_waiting.size()) + (m_current.empty() ? 0 : 1); }
    long long lastCheck() const { return m_lastCheck; } // secondes depuis 1970, 0 = jamais
    // Les cores qui ont une version plus récente (d'après la dernière liste).
    const std::set<std::string>& outdated() const { return m_outdated; }
    std::vector<std::string> takeMessages();

private:
    struct Entry {
        std::string date; // "2026-10-22"
        std::string crc;  // "b66d462e"
    };
    void load();
    void save() const;
    void compare(const std::map<std::string, Entry>& index);

    std::filesystem::path m_dir;
    std::map<std::string, std::string> m_versions; // core -> empreinte installée
    long long m_lastCheck = 0;

    std::thread m_thread;
    std::atomic<bool> m_fetching{false};
    std::mutex m_mutex; // protège m_fetched
    std::optional<std::map<std::string, Entry>> m_fetched;
    std::map<std::string, Entry> m_index; // la dernière liste reçue

    std::set<std::string> m_outdated;
    std::deque<std::string> m_queue;
    std::set<std::string> m_waiting; // en attente que le jeu qui l'utilise soit fermé
    std::string m_current;
    int m_updated = 0;
    int m_failed = 0;
    std::vector<std::string> m_messages;
};
