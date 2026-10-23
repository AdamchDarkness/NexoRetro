#pragma once
// FileHasher : calcule l'empreinte de fichiers de jeu en arrière-plan.
//
// Lire un ISO de PS2 (4 Go) prend plusieurs secondes : le calcul se fait
// dans un thread, et le résultat est gardé dans hashes.txt. Tant que le
// fichier ne change pas (même taille, même date), on ne le relit jamais.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class FileHasher {
public:
    explicit FileHasher(std::filesystem::path cacheFile);
    ~FileHasher();
    FileHasher(const FileHasher&) = delete;
    FileHasher& operator=(const FileHasher&) = delete;

    // Renvoie l'empreinte si elle est connue, sinon lance le calcul et
    // renvoie std::nullopt (redemander à l'image suivante).
    std::optional<uint64_t> request(const std::filesystem::path& file);
    // Avancement du fichier en cours de lecture (0 à 1), pour l'interface.
    float progress() const { return m_progress; }

private:
    struct Entry {
        uint64_t size = 0;
        long long time = 0;
        uint64_t hash = 0;
    };
    void worker();
    void saveCache() const; // appelé avec m_mutex verrouillé
    static bool stamp(const std::filesystem::path& file, uint64_t& size, long long& time);

    std::filesystem::path m_cacheFile;
    std::map<std::string, Entry> m_cache; // clé : chemin en UTF-8
    std::deque<std::filesystem::path> m_queue;
    mutable std::mutex m_mutex;
    // [C++] condition_variable : le thread dort jusqu'à ce qu'on le réveille.
    std::condition_variable m_wake;
    std::atomic<float> m_progress{0.0f};
    bool m_stop = false;
    std::thread m_thread;
};
