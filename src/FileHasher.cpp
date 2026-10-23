#include "FileHasher.hpp"

#include "Hash.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

FileHasher::FileHasher(fs::path cacheFile) : m_cacheFile(std::move(cacheFile))
{
    // Format : <chemin> TAB <taille> TAB <date> TAB <empreinte en hexadécimal>
    std::ifstream in(m_cacheFile);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string path, size, time, hash;
        if (std::getline(s, path, '\t') && std::getline(s, size, '\t') && std::getline(s, time, '\t') &&
            std::getline(s, hash, '\t'))
            m_cache[path] = {std::stoull(size), std::stoll(time), std::stoull(hash, nullptr, 16)};
    }
    m_thread = std::thread([this] { worker(); });
}

FileHasher::~FileHasher()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

bool FileHasher::stamp(const fs::path& file, uint64_t& size, long long& time)
{
    std::error_code ec;
    size = fs::file_size(file, ec);
    if (ec)
        return false;
    time = fs::last_write_time(file, ec).time_since_epoch().count();
    return !ec;
}

std::optional<uint64_t> FileHasher::request(const fs::path& file)
{
    uint64_t size = 0;
    long long time = 0;
    if (!stamp(file, size, time))
        return std::nullopt;
    const std::string key = toUtf8(file);

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_cache.find(key);
    if (it != m_cache.end() && it->second.size == size && it->second.time == time)
        return it->second.hash;
    // Pas encore calculée : on l'ajoute à la file (une seule fois).
    if (std::find(m_queue.begin(), m_queue.end(), file) == m_queue.end()) {
        m_queue.push_back(file);
        m_wake.notify_one();
    }
    return std::nullopt;
}

void FileHasher::worker()
{
    std::vector<char> buffer(4 * 1024 * 1024); // on lit 4 Mo à la fois
    for (;;) {
        fs::path file;
        {
            // [C++] unique_lock : comme lock_guard, mais que wait() peut
            // relâcher pendant que le thread dort.
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                return;
            file = m_queue.front();
        }

        uint64_t size = 0;
        long long time = 0;
        Entry entry;
        bool ok = stamp(file, size, time);
        if (ok) {
            const auto start = std::chrono::steady_clock::now();
            std::ifstream in(file, std::ios::binary);
            Hasher64 hasher;
            uint64_t done = 0;
            m_progress = 0.0f;
            while (in && !m_stop) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize n = in.gcount();
                if (n <= 0)
                    break;
                hasher.update(buffer.data(), static_cast<size_t>(n));
                done += static_cast<uint64_t>(n);
                m_progress = size ? static_cast<float>(done) / static_cast<float>(size) : 1.0f;
            }
            ok = done == size;
            entry = {size, time, hasher.finish()};
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            SDL_Log("Empreinte de %s : %016llx (%lld ms)", toUtf8(file.filename()).c_str(),
                    static_cast<unsigned long long>(entry.hash), static_cast<long long>(ms.count()));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.pop_front();
        if (ok) {
            m_cache[toUtf8(file)] = entry;
            saveCache();
        }
        m_progress = 0.0f;
    }
}

void FileHasher::saveCache() const
{
    std::ofstream out(m_cacheFile, std::ios::trunc);
    char hex[17];
    for (const auto& [path, e] : m_cache) {
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(e.hash));
        out << path << '\t' << e.size << '\t' << e.time << '\t' << hex << '\n';
    }
}
