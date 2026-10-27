#include "CoreUpdater.hpp"
#include "Compat.hpp"

#include "ExternalInstaller.hpp"
#include "net/Http.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const std::string kIndexUrl = std::string(buildbot::kCoresUrl) + ".index-extended";
constexpr long long kCheckEverySeconds = 20 * 3600;

long long nowSeconds()
{
    return static_cast<long long>(std::time(nullptr));
}

// La date d'un fichier, au format "2026-10-22" (comme celles du serveur).
std::string fileDate(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec)
        return {};
    const auto sys = compat::toSystemClock(time);
    const std::time_t t = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &tm);
    return text;
}

} // namespace

CoreUpdater::CoreUpdater(fs::path coresDir) : m_dir(std::move(coresDir))
{
    load();
}

CoreUpdater::~CoreUpdater()
{
    if (m_thread.joinable())
        m_thread.join();
}

void CoreUpdater::load()
{
    std::ifstream in(m_dir / "versions.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string a, b;
        words >> a >> b;
        if (a == "#check")
            m_lastCheck = std::atoll(b.c_str());
        else if (!a.empty() && a[0] != '#' && !b.empty())
            m_versions[a] = b;
    }
}

void CoreUpdater::save() const
{
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    std::ofstream out(m_dir / "versions.txt", std::ios::trunc);
    out << "# Version installée de chaque core (empreinte du serveur de libretro)\n";
    out << "#check " << m_lastCheck << '\n';
    for (const auto& [core, crc] : m_versions)
        out << core << ' ' << crc << '\n';
}

void CoreUpdater::check(bool force)
{
    if (m_fetching || (!force && nowSeconds() - m_lastCheck < kCheckEverySeconds))
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_fetching = true;
    m_thread = std::thread([this] {
        net::HttpClient http("NexoRetro (mise à jour des cores)");
        const net::HttpResponse response = http.get(kIndexUrl);
        std::map<std::string, Entry> index;
        if (response.ok()) {
            std::istringstream lines(response.body);
            std::string date, crc, file;
            while (lines >> date >> crc >> file)
                if (file.ends_with(".zip"))
                    if (const std::string core = buildbot::coreFromServer(file.substr(0, file.size() - 4)); !core.empty())
                        index[core] = {date, crc}; // "mgba_libretro.dll"
        } else {
            SDL_Log("Mise à jour des cores : liste introuvable (%d %s)", response.status, response.error.c_str());
        }
        std::lock_guard lock(m_mutex);
        m_fetched = std::move(index);
        m_fetching = false;
    });
}

void CoreUpdater::compare(const std::map<std::string, Entry>& index)
{
    m_outdated.clear();
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(m_dir, ec)) {
        const std::string file = item.path().filename().string();
        const auto it = index.find(file);
        if (!file.ends_with("_libretro.dll") || it == index.end())
            continue;
        const auto known = m_versions.find(file);
        if (known != m_versions.end()) {
            if (known->second != it->second.crc)
                m_outdated.insert(file);
            continue;
        }
        // Installé avant ce suivi : on compare les dates. Un fichier plus
        // récent que la version du serveur est considéré à jour.
        if (fileDate(item.path()) >= it->second.date)
            m_versions[file] = it->second.crc;
        else
            m_outdated.insert(file);
    }
    save();
}

void CoreUpdater::update(ExternalInstaller& installer, const std::string& coreInUse)
{
    // La liste vient d'arriver : qui est en retard ?
    std::optional<std::map<std::string, Entry>> fetched;
    {
        std::lock_guard lock(m_mutex);
        fetched.swap(m_fetched);
    }
    if (fetched && !fetched->empty()) {
        m_index = std::move(*fetched);
        m_lastCheck = nowSeconds();
        compare(m_index);
        SDL_Log("Mise à jour des cores : %zu core(s) à mettre à jour", m_outdated.size());
        for (const std::string& core : m_outdated)
            if (core != m_current && std::find(m_queue.begin(), m_queue.end(), core) == m_queue.end())
                m_queue.push_back(core);
        m_updated = m_failed = 0;
    }

    // Les cores qui attendaient la fin d'un jeu.
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (*it != coreInUse) {
            m_queue.push_back(*it);
            it = m_waiting.erase(it);
        } else {
            ++it;
        }
    }
    // La suivante, quand l'installateur est libre.
    while (m_current.empty() && !m_queue.empty() && !installer.busy()) {
        const std::string core = m_queue.front();
        m_queue.pop_front();
        if (core == coreInUse) {
            m_waiting.insert(core);
            continue;
        }
        if (installer.startCore(core, m_dir))
            m_current = core;
        else
            m_queue.push_front(core);
        break;
    }
}

bool CoreUpdater::onInstalled(const std::string& coreFile, bool ok)
{
    if (ok) {
        // Installé ou mis à jour (par nous ou à la main) : c'est la version du serveur.
        if (const auto it = m_index.find(coreFile); it != m_index.end())
            m_versions[coreFile] = it->second.crc;
        m_outdated.erase(coreFile);
        save();
    }
    if (coreFile != m_current)
        return false;
    m_current.clear();
    (ok ? m_updated : m_failed) += 1;
    if (m_queue.empty() && m_waiting.empty() && (m_updated + m_failed) > 0) {
        std::string text = m_updated == 1 ? "1 core mis à jour" : std::to_string(m_updated) + " cores mis à jour";
        if (m_failed > 0)
            text += " (" + std::to_string(m_failed) + " en échec)";
        m_messages.push_back(text);
        m_updated = m_failed = 0;
    }
    return true;
}

std::vector<std::string> CoreUpdater::takeMessages()
{
    std::vector<std::string> messages;
    messages.swap(m_messages);
    return messages;
}
