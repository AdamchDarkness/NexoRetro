#include "ExternalInstaller.hpp"

#include "Utf8.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Les pages officielles. "must" / "mustNot" choisissent le bon fichier
// parmi ceux de la version : Windows 64 bits, sans symboles de débogage.
const ExternalInstaller::Source kSources[] = {
    {"ps3", "RPCS3/rpcs3-binaries-win", {"win64", nullptr}, {"sha256", nullptr}, ".7z", "rpcs3.exe"},
    {"ps4", "shadps4-emu/shadPS4", {"win64", "sdl"}, {nullptr, nullptr}, ".zip", "shadPS4.exe"},
    {"xbox", "xemu-project/xemu", {"windows-x86_64", nullptr}, {"dbg", "pdb"}, ".zip", "xemu.exe"},
    {"x360", "xenia-canary/xenia-canary-releases", {"windows", nullptr}, {nullptr, nullptr}, ".zip", "xenia_canary.exe"},
};

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Toutes les valeurs d'une clé dans du JSON ("browser_download_url": "...").
// Une vraie lecture du JSON serait plus propre, mais ces deux champs sont
// de simples textes sans guillemets à l'intérieur : chercher suffit.
std::vector<std::string> jsonValues(const std::string& json, const std::string& key)
{
    std::vector<std::string> values;
    const std::string pattern = "\"" + key + "\"";
    for (size_t pos = json.find(pattern); pos != std::string::npos; pos = json.find(pattern, pos + 1)) {
        size_t start = json.find('"', json.find(':', pos + pattern.size()) + 1);
        if (start == std::string::npos)
            break;
        const size_t end = json.find('"', start + 1);
        values.push_back(json.substr(start + 1, end - start - 1));
    }
    return values;
}

// Lance un programme sans fenêtre et attend qu'il se termine. Renvoie son
// code de sortie (-1 s'il n'a pas démarré).
int runHidden(const std::wstring& commandLine)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring line = commandLine; // CreateProcessW peut modifier la chaîne
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

} // namespace

const ExternalInstaller::Source* ExternalInstaller::sourceFor(const std::string& systemId)
{
    for (const Source& s : kSources)
        if (systemId == s.systemId)
            return &s;
    return nullptr;
}

ExternalInstaller::ExternalInstaller(fs::path emulatorsDir) : m_dir(std::move(emulatorsDir)) {}

ExternalInstaller::~ExternalInstaller()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

bool ExternalInstaller::begin(const std::string& id)
{
    if (m_busy)
        return false;
    if (m_thread.joinable())
        m_thread.join(); // l'installation précédente est finie
    {
        std::lock_guard lock(m_mutex);
        m_system = id;
        m_result.reset();
    }
    m_busy = true;
    m_cancel = false;
    m_progress.received = 0;
    m_progress.total = 0;
    return true;
}

bool ExternalInstaller::start(const std::string& systemId)
{
    const Source* source = sourceFor(systemId);
    if (!source || !begin(systemId))
        return false;
    m_thread = std::thread([this, source] { run(*source); });
    return true;
}

bool ExternalInstaller::startCore(const std::string& coreFile, const fs::path& coresDir)
{
    if (coreFile.empty() || !begin("core:" + coreFile))
        return false;
    // Le "buildbot" de libretro publie chaque core compilé, dans un .zip.
    m_thread = std::thread([this, coreFile, coresDir] {
        runZip("core:" + coreFile, "https://buildbot.libretro.com/nightly/windows/x86_64/latest/" + coreFile + ".zip",
               coresDir, coreFile);
    });
    return true;
}

bool ExternalInstaller::startSystemFiles(const std::string& name, const fs::path& biosDir)
{
    // Dolphin.zip contient dolphin-emu/Sys/ (polices, réglages par jeu,
    // shaders) ; PPSSPP.zip contient PPSSPP/ (polices, compat.ini, flash0).
    // Les fichiers libres des émulateurs eux-mêmes : pas des BIOS de Nintendo
    // ou de Sony. (La PSP n'a pas besoin de BIOS : PPSSPP émule son système.)
    fs::path check;
    if (name == "Dolphin")
        check = fs::path("dolphin-emu") / "Sys" / "GameSettings";
    else if (name == "PPSSPP")
        check = fs::path("PPSSPP") / "compat.ini";
    else
        return false;
    if (!begin("files:" + name))
        return false;
    m_thread = std::thread([this, biosDir, name, check] {
        runZip("files:" + name, "https://buildbot.libretro.com/assets/system/" + name + ".zip", biosDir, check);
    });
    return true;
}

void ExternalInstaller::runZip(const std::string& id, const std::string& url, const fs::path& dest, const fs::path& check)
{
    Result result;
    result.systemId = id;
    net::HttpClient http("NexoRetro (installation)");

    setStep("Téléchargement…");
    const net::HttpResponse archive = http.get(url, &m_cancel, &m_progress);
    if (!archive.ok()) {
        result.message = archive.status == 404 ? "introuvable sur le serveur de libretro"
                                               : "téléchargement impossible" + (archive.error.empty() ? "" : " : " + archive.error);
        return finish(result);
    }
    setStep("Décompression…");
    m_extracting = true;
    std::error_code ec;
    fs::create_directories(dest, ec);
    const fs::path zip = dest / "telechargement.zip";
    {
        std::ofstream out(zip, std::ios::binary);
        out.write(archive.body.data(), static_cast<std::streamsize>(archive.body.size()));
    }
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    const int code = runHidden(L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + zip.wstring() + L"\" -C \"" +
                               dest.wstring() + L"\"");
    fs::remove(zip, ec);
    if (code != 0 || !fs::exists(dest / check, ec)) {
        result.message = "décompression impossible";
        return finish(result);
    }
    result.ok = true;
    result.exe = dest / check;
    result.message = "installé";
    finish(result);
}

std::string ExternalInstaller::busySystem() const
{
    std::lock_guard lock(m_mutex);
    return m_busy ? m_system : std::string();
}

float ExternalInstaller::progress() const
{
    const uint64_t total = m_progress.total;
    if (m_extracting || total == 0)
        return -1.0f;
    return static_cast<float>(m_progress.received) / static_cast<float>(total);
}

std::string ExternalInstaller::stepText() const
{
    std::lock_guard lock(m_mutex);
    return m_step;
}

std::optional<ExternalInstaller::Result> ExternalInstaller::takeResult()
{
    std::lock_guard lock(m_mutex);
    std::optional<Result> result;
    result.swap(m_result);
    return result;
}

void ExternalInstaller::setStep(std::string text)
{
    std::lock_guard lock(m_mutex);
    m_step = std::move(text);
}

void ExternalInstaller::finish(Result result)
{
    SDL_Log("Installation de %s : %s", result.systemId.c_str(), result.message.c_str());
    {
        std::lock_guard lock(m_mutex);
        m_result = std::move(result);
        m_step.clear();
    }
    m_extracting = false;
    m_busy = false;
}

void ExternalInstaller::run(const Source& source)
{
    Result result;
    result.systemId = source.systemId;
    net::HttpClient http("NexoRetro (installation d'emulateurs)");

    // 1. La dernière version publiée, et le bon fichier pour Windows.
    setStep("Recherche de la dernière version…");
    const net::HttpResponse release = http.get(std::string("https://api.github.com/repos/") + source.repo + "/releases/latest", &m_cancel);
    if (!release.ok()) {
        result.message = "GitHub ne répond pas" + (release.status ? " (erreur " + std::to_string(release.status) + ")" : "");
        return finish(result);
    }
    std::string url;
    for (const std::string& candidate : jsonValues(release.body, "browser_download_url")) {
        const std::string name = toLower(candidate.substr(candidate.find_last_of('/') + 1));
        bool fits = name.ends_with(source.extension);
        for (const char* word : source.must)
            fits = fits && (!word || name.find(word) != std::string::npos);
        for (const char* word : source.mustNot)
            fits = fits && (!word || name.find(word) == std::string::npos);
        if (fits) {
            url = candidate;
            break;
        }
    }
    const std::vector<std::string> tags = jsonValues(release.body, "tag_name");
    const std::string version = tags.empty() ? "?" : tags.front();
    if (url.empty()) {
        result.message = "Aucune version Windows trouvée sur la page officielle";
        return finish(result);
    }

    // 2. Le téléchargement.
    setStep("Téléchargement…");
    const net::HttpResponse archive = http.get(url, &m_cancel, &m_progress);
    if (!archive.ok()) {
        result.message = "Téléchargement impossible : " +
                         (archive.error.empty() ? "erreur " + std::to_string(archive.status) : archive.error);
        return finish(result);
    }

    // 3. La décompression, avec tar.exe (fourni avec Windows 10 et 11, il
    //    sait aussi ouvrir les .7z). L'ancienne version est remplacée.
    setStep("Décompression…");
    m_extracting = true;
    std::error_code ec;
    const fs::path home = m_dir / source.systemId;
    const fs::path appDir = home / "app";
    const fs::path archiveFile = home / (std::string("telechargement") + source.extension);
    fs::create_directories(home, ec);
    {
        std::ofstream out(archiveFile, std::ios::binary);
        out.write(archive.body.data(), static_cast<std::streamsize>(archive.body.size()));
        if (!out) {
            result.message = "Impossible d'écrire dans " + toUtf8(home);
            return finish(result);
        }
    }
    fs::remove_all(appDir, ec);
    fs::create_directories(appDir, ec);
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    const int code = runHidden(L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + archiveFile.wstring() + L"\" -C \"" +
                               appDir.wstring() + L"\"");
    fs::remove(archiveFile, ec);
    if (code != 0) {
        result.message = "Décompression impossible (tar : code " + std::to_string(code) + ")";
        return finish(result);
    }

    // 4. Le programme, quelque part dans ce qui a été décompressé.
    const std::string wanted = toLower(source.exe);
    for (auto it = fs::recursive_directory_iterator(appDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file() && toLower(toUtf8(it->path().filename())) == wanted) {
            result.exe = it->path();
            break;
        }
    if (result.exe.empty()) {
        result.message = std::string(source.exe) + " introuvable dans l'archive";
        return finish(result);
    }

    // 5. Mode "portable" : l'émulateur range ses réglages à côté de lui (et
    //    pas dans Documents), pour que tout reste dans le dossier emulators/.
    const fs::path folder = result.exe.parent_path();
    if (std::string(source.systemId) == "x360")
        std::ofstream(folder / "portable.txt"); // Xenia
    if (std::string(source.systemId) == "xbox" && !fs::exists(folder / "xemu.toml", ec))
        std::ofstream(folder / "xemu.toml"); // xemu
    std::ofstream(home / "version.txt") << version << "\n";

    result.ok = true;
    result.message = "installé (version " + version + ")";
    finish(result);
}
