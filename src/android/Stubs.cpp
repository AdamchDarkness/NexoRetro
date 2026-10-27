// Sous Android, ce qui n'existe que sous Windows est remplacé par une
// version neutre (même interface, voir les .hpp) :
//  - Discord (tube nommé de l'appli Discord du PC) : rien à faire ;
//  - émulateurs externes (Switch, PS3, Xbox : des programmes Windows) : indisponibles ;
//  - Corbeille de Windows : indisponible (rien n'est supprimé sans elle) ;
//  - vidéos des runs (Media Foundation) : pas encore sur Android ;
//  - mise à jour : on vérifie la dernière version sur GitHub comme sous
//    Windows, mais l'APK se télécharge depuis la page de la version
//    (Android demande à l'utilisateur de confirmer chaque installation).

#include "../AppUpdater.hpp"
#include "../Discord.hpp"
#include "../ExternalProcess.hpp"
#include "../ExternalSetup.hpp"
#include "../Json.hpp"
#include "../Trash.hpp"
#include "../VideoRecorder.hpp"

#include <SDL3/SDL.h>

#ifndef NEXO_VERSION
#define NEXO_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

// --- Discord ------------------------------------------------------------------------------

DiscordPresence::DiscordPresence() = default;
DiscordPresence::~DiscordPresence() = default;
void DiscordPresence::configure(bool, const std::string&) {}
void DiscordPresence::set(const Activity&) {}
std::string DiscordPresence::status() const
{
    return "Indisponible sur Android";
}

// --- Émulateurs externes ---------------------------------------------------------------------

ExternalProcess::~ExternalProcess() = default;
bool ExternalProcess::start(const fs::path&, const std::vector<std::string>&, std::string& error)
{
    error = "Les émulateurs externes ne fonctionnent que sous Windows";
    return false;
}
bool ExternalProcess::running()
{
    return false;
}

namespace external {
SystemFiles systemFiles(const std::string&, const fs::path&, const fs::path&)
{
    return {};
}
std::string importSystemFiles(const std::string&, const fs::path&, const fs::path&, const std::vector<fs::path>&)
{
    return "Indisponible sur Android";
}
void configure(const std::string&, const fs::path&, const fs::path&) {}
} // namespace external

// --- Corbeille ------------------------------------------------------------------------------

bool moveToTrash(const std::vector<fs::path>&, std::string& error)
{
    // (Sans Corbeille, une suppression serait définitive : on préfère ne rien supprimer.)
    error = "pas de Corbeille sur Android : supprimez le fichier avec le gestionnaire de fichiers";
    return false;
}

// --- Vidéos des runs ------------------------------------------------------------------------

VideoRecorder::~VideoRecorder() = default;
bool VideoRecorder::start(const fs::path&, double, double)
{
    std::lock_guard lock(m_mutex);
    m_error = "Les vidéos des runs ne sont pas encore disponibles sur Android";
    return false;
}
void VideoRecorder::stop() {}
void VideoRecorder::addFrame(std::vector<uint8_t>, int, int, uint64_t) {}
void VideoRecorder::addAudio(const int16_t*, size_t) {}
std::string VideoRecorder::takeError()
{
    std::lock_guard lock(m_mutex);
    std::string e;
    e.swap(m_error);
    return e;
}

// --- Mise à jour ------------------------------------------------------------------------------

namespace {
constexpr const char* kLatestUrl = "https://api.github.com/repos/AdamchDarkness/NexoRetro/releases/latest";

// "v0.5.1" -> {0, 5, 1}
void versionParts(std::string_view text, long long part[3])
{
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
        text.remove_prefix(1);
    size_t i = 0;
    for (int n = 0; n < 3; ++n) {
        part[n] = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            part[n] = part[n] * 10 + (text[i++] - '0');
        if (i < text.size() && text[i] == '.')
            ++i;
    }
}
} // namespace

const char* AppUpdater::currentVersion()
{
    return NEXO_VERSION;
}

int AppUpdater::compareVersions(std::string_view a, std::string_view b)
{
    long long va[3], vb[3];
    versionParts(a, va);
    versionParts(b, vb);
    for (int n = 0; n < 3; ++n)
        if (va[n] != vb[n])
            return va[n] < vb[n] ? -1 : 1;
    return 0;
}

AppUpdater::AppUpdater(fs::path exeDir) : m_exeDir(std::move(exeDir)) {}

AppUpdater::~AppUpdater()
{
    join();
}

void AppUpdater::join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void AppUpdater::check(bool automatic)
{
    if (m_state == State::Checking)
        return;
    join();
    m_state = State::Checking;
    m_automatic = automatic;
    m_error.clear();
    m_thread = std::thread([this] {
        Found found;
        net::HttpClient http("NexoRetro (Android)");
        const net::HttpResponse response = http.get(kLatestUrl);
        found.status = response.status;
        json::Value release;
        if (!response.ok()) {
            found.error = response.status == 0 ? "GitHub injoignable" : "GitHub a répondu " + std::to_string(response.status);
        } else if (json::Parser(response.body).parse(release) && release.type == json::Value::Type::Object) {
            found.ok = true;
            found.version = release.string("tag_name");
            found.name = release.string("name");
            found.notes = release.string("body");
            found.page = release.string("html_url");
        } else {
            found.error = "Réponse de GitHub illisible";
        }
        std::lock_guard lock(m_mutex);
        m_found = std::move(found);
    });
}

void AppUpdater::download()
{
    // L'APK se télécharge depuis la page de la version (navigateur), puis
    // Android demande de confirmer l'installation.
    if (!m_page.empty())
        SDL_OpenURL(m_page.c_str());
}

void AppUpdater::update()
{
    std::optional<Found> found;
    {
        std::lock_guard lock(m_mutex);
        found.swap(m_found);
    }
    if (!found)
        return;
    m_checkFinished = true;
    if (!found->ok) {
        m_state = State::Error;
        m_error = found->error;
        return;
    }
    m_latest = found->version;
    m_name = found->name;
    m_notes = found->notes;
    m_page = found->page;
    m_state = !m_latest.empty() && compareVersions(m_latest, currentVersion()) > 0 ? State::Available : State::UpToDate;
}

float AppUpdater::progress() const
{
    return 0.0f;
}

bool AppUpdater::takeCheckFinished()
{
    const bool done = m_checkFinished;
    m_checkFinished = false;
    return done;
}

bool AppUpdater::scheduleInstall()
{
    return false;
}

void AppUpdater::runPendingInstaller() {}
