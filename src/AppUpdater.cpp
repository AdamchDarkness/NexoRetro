#include "AppUpdater.hpp"

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

// --- Un petit lecteur de JSON ---------------------------------------------
// Le projet n'a pas de bibliothèque JSON, et on n'a besoin que de quelques
// champs. Ce lecteur comprend pourtant tout le format (objets, tableaux,
// textes avec leurs codes d'échappement, nombres, true/false/null) : un
// texte de notes qui contient des guillemets ou des accolades ne risque pas
// de le tromper, contrairement à une simple recherche de texte.
namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    std::string text;               // String : le texte décodé ; Number / Bool : tel qu'écrit
    std::vector<std::string> keys;  // Object : les noms des champs...
    std::vector<Value> items;       // ... et leurs valeurs (Array : les éléments)
    // [C++17] Un std::vector peut contenir le type qu'on est en train de
    // définir (Value contient des Value) : c'est ce qui permet l'arbre.

    // Le champ "key" d'un objet (nullptr s'il n'existe pas).
    const Value* get(std::string_view key) const
    {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }
    // Le texte du champ "key", ou "" s'il manque (ou n'est pas un texte).
    std::string string(std::string_view key) const
    {
        const Value* v = get(key);
        return v && v->type == Type::String ? v->text : std::string();
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_s(text) {}

    bool parse(Value& out)
    {
        if (!value(out, 0))
            return false;
        skipSpaces();
        return m_i == m_s.size(); // rien d'autre après
    }

private:
    void skipSpaces()
    {
        while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r'))
            ++m_i;
    }
    bool literal(std::string_view word)
    {
        if (m_s.substr(m_i, word.size()) != word)
            return false;
        m_i += word.size();
        return true;
    }

    bool value(Value& out, int depth)
    {
        if (depth > 64) // un JSON piégé, imbriqué à l'infini, ne fait pas déborder la pile
            return false;
        skipSpaces();
        if (m_i >= m_s.size())
            return false;
        const char c = m_s[m_i];
        if (c == '{')
            return object(out, depth);
        if (c == '[')
            return array(out, depth);
        if (c == '"') {
            out.type = Value::Type::String;
            return string(out.text);
        }
        if (c == 't' || c == 'f') {
            out.type = Value::Type::Bool;
            out.text = c == 't' ? "true" : "false";
            return literal(out.text);
        }
        if (c == 'n') {
            out.type = Value::Type::Null;
            return literal("null");
        }
        // Un nombre : on le garde tel quel (on n'en lit aucun).
        const size_t start = m_i;
        while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '-' ||
                                    m_s[m_i] == '+' || m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E'))
            ++m_i;
        if (m_i == start)
            return false;
        out.type = Value::Type::Number;
        out.text = std::string(m_s.substr(start, m_i - start));
        return true;
    }

    bool object(Value& out, int depth)
    {
        out.type = Value::Type::Object;
        ++m_i; // '{'
        skipSpaces();
        if (m_i < m_s.size() && m_s[m_i] == '}') {
            ++m_i;
            return true;
        }
        for (;;) {
            skipSpaces();
            std::string key;
            if (m_i >= m_s.size() || m_s[m_i] != '"' || !string(key))
                return false;
            skipSpaces();
            if (m_i >= m_s.size() || m_s[m_i] != ':')
                return false;
            ++m_i;
            Value item;
            if (!value(item, depth + 1))
                return false;
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(item));
            skipSpaces();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == '}') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    bool array(Value& out, int depth)
    {
        out.type = Value::Type::Array;
        ++m_i; // '['
        skipSpaces();
        if (m_i < m_s.size() && m_s[m_i] == ']') {
            ++m_i;
            return true;
        }
        for (;;) {
            Value item;
            if (!value(item, depth + 1))
                return false;
            out.items.push_back(std::move(item));
            skipSpaces();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == ']') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    // 4 chiffres hexadécimaux après "\u".
    bool hex4(unsigned& out)
    {
        if (m_i + 4 > m_s.size())
            return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = m_s[m_i++];
            out <<= 4;
            if (h >= '0' && h <= '9') out |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') out |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') out |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }

    // Écrit le caractère numéro "code" (Unicode) en UTF-8 : 1 à 4 octets.
    static void appendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string& out)
    {
        ++m_i; // '"'
        while (m_i < m_s.size()) {
            const char c = m_s[m_i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c; // (l'UTF-8 passe tel quel)
                continue;
            }
            if (m_i >= m_s.size())
                return false;
            const char e = m_s[m_i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code))
                    return false;
                // Les caractères au-delà de U+FFFF (émojis...) s'écrivent en
                // deux morceaux, "🎮" : une "paire de substitution".
                if (code >= 0xD800 && code <= 0xDBFF && m_s.substr(m_i, 2) == "\\u") {
                    m_i += 2;
                    unsigned low = 0;
                    if (!hex4(low))
                        return false;
                    if (low >= 0xDC00 && low <= 0xDFFF)
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false; // texte jamais refermé
    }

    std::string_view m_s;
    size_t m_i = 0;
};

} // namespace json

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
