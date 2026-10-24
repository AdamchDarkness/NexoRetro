#include "PortableCopy.hpp"

#include "Library.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Ce qui fait partie des données (le dossier peut contenir autre chose :
// celui d'une compilation de développement est plein de fichiers de build).
const char* const kDataEntries[] = {"bios",       "cheats",        "core-options", "core-options.txt", "cores",
                                    "covers",     "emulators",     "hashes.txt",   "input.ini",        "remaps.txt",
                                    "roms",       "saves",         "scraper",      "settings.ini",     "themes"};

struct Copy {
    fs::path from;
    fs::path to;
    std::uintmax_t size = 0;
};

// Tous les fichiers d'un fichier ou d'un dossier, avec leur destination.
void collect(const fs::path& from, const fs::path& to, std::vector<Copy>& out)
{
    std::error_code ec;
    if (fs::is_regular_file(from, ec)) {
        out.push_back({from, to, fs::file_size(from, ec)});
        return;
    }
    for (auto it = fs::recursive_directory_iterator(from, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc))
            out.push_back({it->path(), to / it->path().lexically_relative(from), it->file_size(fileEc)});
    }
}

std::string sizeText(std::uintmax_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f Go", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return text;
}

} // namespace

PortableCopy::~PortableCopy()
{
    if (m_thread.joinable())
        m_thread.join();
}

void PortableCopy::start(Plan plan)
{
    if (m_busy)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_busy = true;
    m_progress = 0.0f;
    // [C++] std::move : le plan (et sa copie des données) part dans le thread.
    m_thread = std::thread([this, plan = std::move(plan)]() mutable { run(std::move(plan)); });
}

std::optional<std::string> PortableCopy::takeResult()
{
    std::lock_guard lock(m_mutex);
    std::optional<std::string> result;
    result.swap(m_result);
    return result;
}

void PortableCopy::run(Plan plan)
{
    auto finish = [&](std::string message) {
        SDL_Log("Copie portable : %s", message.c_str());
        std::lock_guard lock(m_mutex);
        m_result = std::move(message);
        m_busy = false;
    };
    const fs::path root = plan.dest / "NexoRetro";
    std::error_code ec;
    if (fs::equivalent(root, plan.exeDir, ec) || fs::equivalent(root, plan.dataDir, ec))
        return finish("Choisissez un autre dossier que celui de Nexo Retro");

    // 1. La liste de tout ce qu'il faut copier.
    std::vector<Copy> copies;
    collect(plan.exeDir / "NexoRetro.exe", root / "NexoRetro.exe", copies);
    collect(plan.exeDir / "assets", root / "assets", copies);
    for (const char* entry : kDataEntries)
        if (fs::exists(plan.dataDir / entry, ec))
            collect(plan.dataDir / entry, root / entry, copies);
    std::map<std::string, std::string> moved; // ancienne clé -> nouveau chemin
    for (const auto& [game, systemId] : plan.outsideGames) {
        for (const fs::path& part : Library::filesOf(game))
            collect(part, root / "roms" / fromUtf8(systemId) / part.filename(), copies);
        moved[UserData::keyOf(game)] = UserData::keyOf(root / "roms" / fromUtf8(systemId) / game.filename());
    }
    std::uintmax_t total = 0;
    for (const Copy& c : copies)
        total += c.size;

    // 2. Assez de place ?
    fs::create_directories(root, ec);
    if (ec)
        return finish("Impossible d'écrire dans " + toUtf8(plan.dest));
    const fs::space_info space = fs::space(root, ec);
    if (!ec && space.available < total)
        return finish("Pas assez de place : il faut " + sizeText(total) + ", il reste " + sizeText(space.available));

    // 3. La copie. Un fichier déjà là, de même taille, est gardé : refaire
    // la copie sur la même clé ne recopie que ce qui a changé... ou presque.
    std::uintmax_t done = 0;
    int failed = 0;
    for (const Copy& c : copies) {
        std::error_code fileEc;
        const bool same = fs::exists(c.to, fileEc) && fs::file_size(c.to, fileEc) == c.size &&
                          fs::last_write_time(c.to, fileEc) >= fs::last_write_time(c.from, fileEc);
        if (!same) {
            fs::create_directories(c.to.parent_path(), fileEc);
            if (!fs::copy_file(c.from, c.to, fs::copy_options::overwrite_existing, fileEc) || fileEc) {
                SDL_Log("Copie portable : échec pour %s (%s)", toUtf8(c.from).c_str(), fileEc.message().c_str());
                ++failed;
            }
        }
        done += c.size;
        m_progress = total ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 1.0f;
    }

    // 4. Les données utilisateur (temps de jeu, favoris...) avec les
    // nouveaux chemins, et le marqueur "portable".
    plan.userData.exportTo(root / "userdata.txt", moved);
    std::ofstream(root / "portable.txt")
        << "Copie portable de Nexo Retro : les données sont rangées à côté de NexoRetro.exe.\n";
    m_progress = 1.0f;
    if (failed > 0)
        return finish("Copie portable terminée, mais " + std::to_string(failed) + " fichier(s) n'ont pas pu être copiés");
    finish("Copie portable prête : " + toUtf8(root) + " (" + sizeText(total) + ")");
}
