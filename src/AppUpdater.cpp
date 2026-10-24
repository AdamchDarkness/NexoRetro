#include "AppUpdater.hpp"

#include "Json.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

#ifndef NEXO_VERSION
#define NEXO_VERSION "0.0.0" // (normalement fourni par CMakeLists.txt)
#endif

namespace {

// La page de l'API qui décrit la dernière version publiée du dépôt.
// EMU_UPDATE_URL la remplace pendant les tests (un faux serveur local).
constexpr const char* kLatestUrl = "https://api.github.com/repos/AdamchDarkness/NexoRetro/releases/latest";

// (Le lecteur JSON, qui servait ici, est maintenant partagé : Json.hpp.)

// Les nombres d'une version : "v0.10.2-beta" -> {0, 10, 2}, suffixe "beta".
struct Version {
    long long part[3] = {0, 0, 0};
    std::string suffix;
};

Version parseVersion(std::string_view text)
{
    Version v;
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
        text.remove_prefix(1);
    size_t i = 0;
    for (int n = 0; n < 3; ++n) {
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
            v.part[n] = v.part[n] * 10 + (text[i++] - '0');
        if (i < text.size() && text[i] == '.')
            ++i;
        else
            break;
    }
    if (i < text.size() && (text[i] == '-' || text[i] == '+'))
        v.suffix = std::string(text.substr(i + 1));
    return v;
}

// Le texte sans espaces ni retours à la ligne autour.
std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    return text.substr(start);
}

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// L'empreinte SHA-256 de données, en 64 chiffres hexadécimaux minuscules.
// Calculée par Windows (BCrypt, sa bibliothèque de cryptographie) : rien à
// recoder, et c'est le même calcul que "certutil -hashfile x SHA256".
std::string sha256Hex(std::string_view data)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return {};
    unsigned char hash[32] = {};
    // [C++] const_cast : BCryptHash ne modifie pas les données, mais sa
    // déclaration (en C) demande un pointeur non-const.
    const NTSTATUS status =
        BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                   static_cast<ULONG>(data.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status))
        return {};
    static const char* const digits = "0123456789abcdef";
    std::string out;
    for (const unsigned char b : hash) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// L'empreinte annoncée pour "name" dans SHA256SUMS.txt. Chaque ligne a la
// forme "<empreinte>  <nom>" (format de l'outil sha256sum ; un "*" devant
// le nom veut dire "fichier binaire").
std::string expectedHash(const std::string& sums, const std::string& name)
{
    std::istringstream lines(sums);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string hash, file;
        words >> hash >> file;
        if (!file.empty() && file[0] == '*')
            file.erase(0, 1);
        if (file == name && hash.size() == 64)
            return toLower(hash);
    }
    return {};
}

std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

const char* AppUpdater::currentVersion()
{
    return NEXO_VERSION;
}

int AppUpdater::compareVersions(std::string_view a, std::string_view b)
{
    const Version va = parseVersion(a);
    const Version vb = parseVersion(b);
    for (int n = 0; n < 3; ++n)
        if (va.part[n] != vb.part[n])
            return va.part[n] < vb.part[n] ? -1 : 1;
    // Mêmes nombres : une version d'essai ("1.0.0-beta") passe AVANT la
    // version finale ("1.0.0").
    if (va.suffix.empty() != vb.suffix.empty())
        return va.suffix.empty() ? 1 : -1;
    return va.suffix.compare(vb.suffix) < 0 ? -1 : (va.suffix == vb.suffix ? 0 : 1);
}

AppUpdater::AppUpdater(fs::path exeDir) : m_exeDir(std::move(exeDir))
{
    std::error_code ec;
    m_installed = fs::is_regular_file(m_exeDir / "installed.txt", ec);
}

AppUpdater::~AppUpdater()
{
    m_cancel = true; // un téléchargement en cours s'arrête tout de suite
    join();
}

void AppUpdater::join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void AppUpdater::check(bool automatic)
{
    // (Prête à installer : on garde l'installateur déjà vérifié.)
    if (m_state == State::Checking || m_state == State::Downloading || m_state == State::Ready)
        return;
    join();
    m_state = State::Checking;
    m_automatic = automatic;
    m_error.clear();
    m_thread = std::thread([this] {
        const char* custom = SDL_getenv("EMU_UPDATE_URL");
        const std::string url = custom && *custom ? custom : kLatestUrl;
        // GitHub refuse les requêtes sans "User-Agent" : on se présente.
        const std::string agent = std::string("NexoRetro/") + NEXO_VERSION;
        net::HttpClient http(agent.c_str());
        const net::HttpResponse response = http.get(url, &m_cancel);

        Found found;
        found.status = response.status;
        json::Value release;
        if (!response.ok()) {
            // 404 : aucune version publiée (ou dépôt encore privé : GitHub
            // fait alors comme s'il n'existait pas).
            if (response.status == 404)
                found.error = "Impossible de vérifier les mises à jour : aucune version publique trouvée";
            else if (response.status == 0)
                found.error = "Impossible de vérifier les mises à jour : pas de connexion";
            else
                found.error = "Impossible de vérifier les mises à jour (erreur " + std::to_string(response.status) + ")";
            SDL_Log("Mise à jour de l'appli : %d %s", response.status, response.error.c_str());
        } else if (!json::Parser(response.body).parse(release) || release.type != json::Value::Type::Object ||
                   release.string("tag_name").empty()) {
            found.error = "Impossible de vérifier les mises à jour : réponse illisible";
        } else {
            found.ok = true;
            std::string tag = release.string("tag_name");
            found.version = !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
            found.name = release.string("name");
            found.notes = trim(release.string("body"));
            found.page = release.string("html_url");
            // Les fichiers joints : l'installateur et la liste des empreintes.
            const std::string wanted = "NexoRetro-Setup-" + found.version + ".exe";
            if (const json::Value* assets = release.get("assets")) {
                for (const json::Value& asset : assets->items) {
                    const std::string name = asset.string("name");
                    const std::string link = asset.string("browser_download_url");
                    if (name == "SHA256SUMS.txt") {
                        found.sumsUrl = link;
                    } else if (name == wanted ||
                               (found.setupUrl.empty() && name.starts_with("NexoRetro-Setup-") && name.ends_with(".exe"))) {
                        found.setupUrl = link;
                        found.setupName = name;
                    }
                }
            }
        }
        std::lock_guard lock(m_mutex);
        m_found = std::move(found);
    });
}

void AppUpdater::download()
{
    if (m_state != State::Available && m_state != State::Error)
        return;
    if (m_latest.empty() || compareVersions(m_latest, currentVersion()) <= 0)
        return; // (Error d'une vérification, ou déjà à jour : rien à télécharger)
    if (m_setupUrl.empty() || m_sumsUrl.empty()) {
        m_state = State::Error;
        m_error = m_setupUrl.empty() ? "Pas d'installateur dans cette version : utilisez la page de téléchargement"
                                     : "Empreinte SHA-256 introuvable (SHA256SUMS.txt) : installation refusée";
        return;
    }
    join();
    m_state = State::Downloading;
    m_error.clear();
    m_progress.received = 0;
    m_progress.total = 0;
    m_cancel = false;
    m_thread = std::thread([this, setupUrl = m_setupUrl, setupName = m_setupName, sumsUrl = m_sumsUrl] {
        const std::string agent = std::string("NexoRetro/") + NEXO_VERSION;
        net::HttpClient http(agent.c_str());
        Downloaded result;
        auto done = [&] {
            std::lock_guard lock(m_mutex);
            m_downloaded = std::move(result);
        };

        // 1. L'empreinte attendue, publiée avec la version.
        const net::HttpResponse sums = http.get(sumsUrl, &m_cancel);
        const std::string expected = sums.ok() ? expectedHash(sums.body, setupName) : std::string();
        if (expected.empty()) {
            result.error = "Empreinte SHA-256 introuvable (SHA256SUMS.txt) : installation refusée";
            return done();
        }
        // 2. L'installateur lui-même (quelques dizaines de Mo).
        const net::HttpResponse setup = http.get(setupUrl, &m_cancel, &m_progress);
        if (!setup.ok()) {
            result.error = "Téléchargement impossible";
            SDL_Log("Mise à jour de l'appli : téléchargement %d %s", setup.status, setup.error.c_str());
            return done();
        }
        // 3. La vérification : la moindre différence, et on refuse.
        const std::string actual = sha256Hex(setup.body);
        if (actual != expected) {
            SDL_Log("Mise à jour de l'appli : empreinte %s, attendue %s", actual.c_str(), expected.c_str());
            result.error = "Empreinte incorrecte : le fichier téléchargé a été refusé";
            return done();
        }
        // 4. Dans le dossier temporaire de Windows (%TEMP%\NexoRetro).
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "NexoRetro";
        fs::create_directories(dir, ec);
        const fs::path file = dir / fromUtf8(setupName);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(setup.body.data(), static_cast<std::streamsize>(setup.body.size()));
            if (!out) {
                result.error = "Impossible d'écrire dans le dossier temporaire";
                return done();
            }
        }
        SDL_Log("Mise à jour de l'appli : %s téléchargé et vérifié (SHA-256 %s)", setupName.c_str(), actual.c_str());
        result.ok = true;
        result.file = file;
        result.hash = actual;
        done();
    });
}

void AppUpdater::update()
{
    std::optional<Found> found;
    std::optional<Downloaded> downloaded;
    {
        std::lock_guard lock(m_mutex);
        found.swap(m_found);
        downloaded.swap(m_downloaded);
    }
    if (found) {
        join();
        m_checkFinished = true;
        if (!found->ok) {
            m_state = State::Error;
            m_error = found->error;
            m_latest.clear(); // (une ancienne réponse ne compte plus)
        } else {
            m_latest = found->version;
            m_name = found->name;
            m_notes = found->notes;
            m_page = found->page;
            m_setupUrl = found->setupUrl;
            m_setupName = found->setupName;
            m_sumsUrl = found->sumsUrl;
            const bool newer = compareVersions(m_latest, currentVersion()) > 0;
            m_state = newer ? State::Available : State::UpToDate;
            SDL_Log("Mise à jour de l'appli : dernière version %s, la nôtre %s -> %s", m_latest.c_str(), currentVersion(),
                    newer ? "mise à jour disponible" : "à jour");
        }
    }
    if (downloaded) {
        join();
        if (downloaded->ok) {
            m_state = State::Ready;
            m_setupFile = downloaded->file;
            m_setupHash = downloaded->hash;
        } else {
            m_state = State::Error;
            m_error = downloaded->error;
        }
    }
}

float AppUpdater::progress() const
{
    const uint64_t total = m_progress.total;
    if (total == 0)
        return -1.0f;
    return static_cast<float>(m_progress.received) / static_cast<float>(total);
}

bool AppUpdater::takeCheckFinished()
{
    const bool finished = m_checkFinished;
    m_checkFinished = false;
    return finished;
}

bool AppUpdater::scheduleInstall()
{
    if (m_state != State::Ready || !m_installed)
        return false;
    // Le fichier a pu être touché depuis le téléchargement (il dort dans le
    // dossier temporaire) : on revérifie son empreinte juste avant.
    if (sha256Hex(readFile(m_setupFile)) != m_setupHash) {
        m_state = State::Error;
        m_error = "Empreinte incorrecte : le fichier téléchargé a été refusé";
        return false;
    }
    s_pendingInstaller = m_setupFile;
    return true;
}

void AppUpdater::runPendingInstaller()
{
    if (s_pendingInstaller.empty())
        return;
    // ShellExecuteEx, et non CreateProcess : si l'installateur demande les
    // droits d'administrateur, Windows affiche sa fenêtre de confirmation
    // (UAC) au lieu d'échouer.
    // /VERYSILENT : aucune fenêtre ; /SUPPRESSMSGBOXES : aucune question ;
    // /NORESTART : jamais de redémarrage du PC.
    const std::wstring file = s_pendingInstaller.wstring();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpParameters = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info))
        SDL_Log("Mise à jour de l'appli : installateur lancé (%s)", toUtf8(s_pendingInstaller).c_str());
    else
        SDL_Log("Mise à jour de l'appli : impossible de lancer l'installateur (erreur %lu)", GetLastError());
    s_pendingInstaller.clear();
}
