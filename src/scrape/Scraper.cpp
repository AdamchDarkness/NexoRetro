#include "Scraper.hpp"

#include "../Hash.hpp"
#include "../PortablePath.hpp"
#include "../UserData.hpp"
#include "../Utf8.hpp"
#include "../net/Http.hpp"
#include "GameDatabase.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace scrape {

namespace {

constexpr const char* kDatabaseUrl = "https://raw.githubusercontent.com/libretro/libretro-database/master/rdb/";
constexpr const char* kThumbnailsUrl = "https://thumbnails.libretro.com/";
constexpr const char* kCheatsUrl = "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/";
constexpr int kRefreshDays = 30;                      // bases et listes : retéléchargées tous les 30 jours
constexpr std::uintmax_t kMaxCrcSize = 512ull << 20;  // au-delà, pas de CRC (trop long à lire)
constexpr size_t kMaxChoices = 12;

long long now()
{
    return static_cast<long long>(std::time(nullptr));
}

std::string lowerExtension(const fs::path& file)
{
    std::string ext = toUtf8(file.extension());
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Les noms des consoles chez libretro (bases ET images), dans l'ordre où
// il faut les essayer. Certaines de nos "consoles" en regroupent deux.
std::vector<std::string> systemNames(const std::string& id, const std::string& ext)
{
    if (id == "nes")
        return {ext == ".fds" ? "Nintendo - Family Computer Disk System" : "Nintendo - Nintendo Entertainment System"};
    if (id == "snes") return {"Nintendo - Super Nintendo Entertainment System"};
    if (id == "n64") return {"Nintendo - Nintendo 64"};
    if (id == "gb") return {"Nintendo - Game Boy", "Nintendo - Game Boy Color"};
    if (id == "gbc") return {"Nintendo - Game Boy Color", "Nintendo - Game Boy"};
    if (id == "gba") return {"Nintendo - Game Boy Advance"};
    if (id == "nds") return {"Nintendo - Nintendo DS"};
    if (id == "gc") return {"Nintendo - GameCube"};
    if (id == "wii") return {"Nintendo - Wii"};
    if (id == "md") return {"Sega - Mega Drive - Genesis"};
    if (id == "segacd") return {"Sega - Mega-CD - Sega CD"};
    if (id == "saturn") return {"Sega - Saturn"};
    if (id == "a2600") return {"Atari - 2600"};
    if (id == "a5200") return {"Atari - 5200"};
    if (id == "a7800") return {"Atari - 7800"};
    if (id == "lynx") return {"Atari - Lynx"};
    if (id == "jaguar") return {"Atari - Jaguar"};
    if (id == "ps1") return {"Sony - PlayStation"};
    if (id == "ps2") return {"Sony - PlayStation 2"};
    if (id == "psp") return {"Sony - PlayStation Portable"};
    if (id == "dc") return {"Sega - Dreamcast"};
    if (id == "3ds") return {"Nintendo - Nintendo 3DS"};
    if (id == "xbox") return {"Microsoft - Xbox"};
    if (id == "x360") return {"Microsoft - Xbox 360"};
    if (id == "ps3") return {"Sony - PlayStation 3"};
    if (id == "ps4") return {"Sony - PlayStation 4"};
    // L'arcade : FBNeo d'abord (mêmes noms de fichiers que le core FBNeo), puis MAME.
    if (id == "arcade") return {"FBNeo - Arcade Games", "MAME"};
    if (id == "neogeo") return {"FBNeo - Arcade Games", "SNK - Neo Geo"};
    return {};
}

// Les consoles à cartouche : leurs fichiers sont petits, et les bases
// connaissent leur CRC. (Pour les CD/DVD, le CRC dépend du format de
// l'image disque : .chd, .iso... il ne servirait à rien.)
bool isCartridge(const std::string& id)
{
    static const char* const ids[] = {"nes", "snes", "n64", "gb", "gbc", "gba", "nds", "md",
                                      "a2600", "a5200", "a7800", "lynx", "jaguar"};
    for (const char* c : ids)
        if (id == c)
            return true;
    return false;
}

// Les images libretro remplacent les caractères interdits dans un nom de
// fichier par "_" : "Mario & Luigi" -> "Mario _ Luigi".
std::string thumbnailName(std::string name)
{
    for (char& c : name)
        if (std::strchr("&*/:`<>?\\|\"", c))
            c = '_';
    return name;
}

// Où ranger une image téléchargée. Les noms officiels dépassent parfois
// 100 caractères ("The King of Fighters '98 - The Slugfest _ ...") : avec
// le chemin du dossier, on franchirait la limite de 260 caractères des
// chemins Windows. Au-delà de 80, on raccourcit et on ajoute une empreinte
// du nom complet (deux noms différents ne donnent jamais le même fichier).
fs::path imageFile(const fs::path& dir, const std::string& system, const char* type, const std::string& name)
{
    std::string file = name;
    if (file.size() > 80) {
        size_t cut = 60;
        // Ne pas couper au milieu d'un caractère UTF-8 (octets 10xxxxxx).
        while (cut > 0 && (static_cast<unsigned char>(file[cut]) & 0xC0) == 0x80)
            --cut;
        char hash[16];
        std::snprintf(hash, sizeof(hash), "%08x", crc32(0, name.data(), name.size()));
        file = file.substr(0, cut) + " ~" + hash;
    }
    return dir / "images" / fromUtf8(system) / type / fromUtf8(file + ".png");
}

// CRC32 du contenu du jeu, tel que les bases le connaissent : sans les
// en-têtes ajoutés par certains formats, et dans l'ordre d'octets standard.
bool fileCrc(const fs::path& file, const std::string& systemId, uint32_t& crc, const std::atomic<bool>& stop)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    std::vector<char> buffer(4 * 1024 * 1024);
    crc = 0;
    bool first = true;
    enum class N64Order { Native, Swap16, Swap32 } order = N64Order::Native;
    while (in && !stop) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        size_t n = static_cast<size_t>(in.gcount());
        if (n == 0)
            break;
        char* data = buffer.data();
        if (first) {
            first = false;
            // NES/FDS : en-tête iNES/fwNES de 16 octets ajouté aux dumps.
            if ((systemId == "nes") && n >= 16 &&
                (std::memcmp(data, "NES\x1A", 4) == 0 || std::memcmp(data, "FDS\x1A", 4) == 0)) {
                data += 16;
                n -= 16;
            }
            // SNES : en-tête de 512 octets des anciens copieurs de cartouches.
            if (systemId == "snes" && size % 1024 == 512 && n >= 512) {
                data += 512;
                n -= 512;
            }
            // N64 : trois ordres d'octets existent (.z64, .v64, .n64) ; les
            // bases utilisent celui du .z64 (80 37 12 40).
            if (systemId == "n64" && n >= 4) {
                const auto* b = reinterpret_cast<const unsigned char*>(data);
                if (b[0] == 0x37 && b[1] == 0x80)
                    order = N64Order::Swap16;
                else if (b[0] == 0x40 && b[1] == 0x12)
                    order = N64Order::Swap32;
            }
        }
        if (order == N64Order::Swap16)
            for (size_t i = 0; i + 1 < n; i += 2)
                std::swap(data[i], data[i + 1]);
        else if (order == N64Order::Swap32)
            for (size_t i = 0; i + 3 < n; i += 4) {
                std::swap(data[i], data[i + 3]);
                std::swap(data[i + 1], data[i + 2]);
            }
        crc = crc32(crc, data, n);
    }
    return !stop;
}

bool isFresh(const fs::path& file, int days)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec)
        return false;
    return fs::file_time_type::clock::now() - time < std::chrono::hours(24 * days);
}

bool writeFile(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // On écrit dans un fichier temporaire puis on le renomme : un fichier à
    // moitié écrit (coupure, fermeture) ne passe jamais pour un fichier complet.
    fs::path temp = file;
    temp += ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out)
            return false;
    }
    fs::rename(temp, file, ec);
    return !ec;
}

std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// La liste des images d'une console, extraite de la page "Index of" du serveur.
struct Listing {
    NameIndex index;
    std::unordered_set<std::string> names;
};

std::vector<std::string> parseListing(const std::string& html)
{
    std::vector<std::string> names;
    size_t pos = 0;
    while ((pos = html.find("href=\"", pos)) != std::string::npos) {
        pos += 6;
        const size_t end = html.find('"', pos);
        if (end == std::string::npos)
            break;
        std::string href = html.substr(pos, end - pos);
        pos = end;
        if (href.size() < 5 || href.compare(href.size() - 4, 4, ".png") != 0)
            continue;
        // "&amp;" dans le HTML = "&" ; puis "%20" = espace...
        for (size_t amp; (amp = href.find("&amp;")) != std::string::npos;)
            href.replace(amp, 5, "&");
        std::string name = net::urlDecode(href);
        name.resize(name.size() - 4);
        if (name.find('/') == std::string::npos)
            names.push_back(std::move(name));
    }
    return names;
}

} // namespace

// Ce que le thread garde en mémoire entre deux jeux.
struct Scraper::Cache {
    net::HttpClient http{"NexoRetro/0.2 (frontend libretro open source)"};
    std::map<std::string, std::unique_ptr<GameDatabase>> databases; // nullptr = indisponible
    std::map<std::string, std::unique_ptr<Listing>> listings;
    bool networkError = false; // le dernier téléchargement a échoué faute de réseau

    // Télécharge url dans file, sauf si une copie récente existe déjà.
    // En cas d'échec, une ancienne copie fait l'affaire.
    bool fetch(const std::string& url, const fs::path& file, int maxAgeDays, const std::atomic<bool>& stop)
    {
        std::error_code ec;
        const bool exists = fs::exists(file, ec);
        if (exists && (maxAgeDays <= 0 || isFresh(file, maxAgeDays)))
            return true;
        const net::HttpResponse r = http.get(url, &stop);
        if (r.ok()) {
            networkError = false;
            return writeFile(file, r.body);
        }
        if (r.status == 0) {
            networkError = true;
            SDL_Log("Scraper : %s (%s)", r.error.c_str(), url.c_str());
        }
        return exists;
    }

    // La base d'une console (téléchargée au premier besoin). nullptr si indisponible.
    const GameDatabase* database(const std::string& system, const fs::path& dir, const std::atomic<bool>& stop)
    {
        auto it = databases.find(system);
        if (it == databases.end()) {
            const fs::path file = dir / "db" / fromUtf8(system + ".rdb");
            auto db = std::make_unique<GameDatabase>();
            if (!fetch(kDatabaseUrl + net::urlEncode(system) + ".rdb", file, kRefreshDays, stop) || !db->load(readFile(file)))
                db.reset();
            else
                SDL_Log("Scraper : base \"%s\" chargée (%zu jeux)", system.c_str(), db->size());
            it = databases.emplace(system, std::move(db)).first;
        }
        return it->second.get();
    }

    // La liste des jaquettes d'une console. nullptr si indisponible.
    const Listing* listing(const std::string& system, const fs::path& dir, const std::atomic<bool>& stop)
    {
        auto it = listings.find(system);
        if (it != listings.end())
            return it->second.get();
        // On garde la liste sous forme de texte (un nom par ligne) : 10 fois
        // plus petit que la page HTML du serveur.
        const fs::path file = dir / "lists" / fromUtf8(system + ".txt");
        if (!isFresh(file, kRefreshDays)) {
            const net::HttpResponse r = http.get(kThumbnailsUrl + net::urlEncode(system) + "/Named_Boxarts/", &stop);
            if (r.ok()) {
                std::string text;
                for (const std::string& n : parseListing(r.body))
                    text += n + '\n';
                writeFile(file, text);
            } else if (r.status == 0) {
                networkError = true;
            }
        }
        std::unique_ptr<Listing> list;
        std::istringstream lines(readFile(file));
        std::string name;
        while (std::getline(lines, name)) {
            if (name.empty())
                continue;
            if (!list)
                list = std::make_unique<Listing>();
            list->index.add(name);
            list->names.insert(name);
        }
        // Sans réseau, on ne retient pas l'échec : on réessaiera au prochain jeu.
        if (!list && networkError)
            return nullptr;
        return listings.emplace(system, std::move(list)).first->second.get();
    }
};

Scraper::Scraper(fs::path dir) : m_dir(std::move(dir)), m_cache(std::make_unique<Cache>())
{
    load();
    m_thread = std::thread([this] { worker(); });
}

Scraper::~Scraper()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true; // arrête aussi un téléchargement en cours
    }
    m_wake.notify_all();
    m_thread.join();
}

// --- Thread principal ---------------------------------------------------

const GameInfo* Scraper::find(const fs::path& game) const
{
    const auto it = m_infos.find(UserData::keyOf(game));
    return it == m_infos.end() ? nullptr : &it->second;
}

bool Scraper::queued(const fs::path& game) const
{
    const std::string key = UserData::keyOf(game);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Job& j : m_queue)
        if (j.kind == Job::Kind::Game && j.key == key)
            return true;
    return false;
}

void Scraper::request(const Game& game, bool force)
{
    const std::string key = UserData::keyOf(game.path);
    if (!force && m_infos.count(key))
        return;
    if (queued(game.path))
        return;
    Job job;
    job.key = key;
    job.path = game.path;
    job.systemId = game.system->id;
    job.fileTitle = cleanTitle(toUtf8(game.path.stem()));
    job.title = game.title;
    job.region = game.region;
    job.size = game.size;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void Scraper::requestMissing(const std::vector<Game>& games)
{
    for (const Game& g : games)
        if (!m_infos.count(UserData::keyOf(g.path)))
            request(g);
}

int Scraper::pending() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int n = m_working ? 1 : 0;
    for (const Job& j : m_queue)
        n += j.kind == Job::Kind::Game;
    return n;
}

std::string Scraper::current() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current;
}

void Scraper::searchCheats(const Game& game)
{
    Job job;
    job.kind = Job::Kind::CheatSearch;
    job.systemId = game.system->id;
    job.path = game.path;
    job.fileTitle = cleanTitle(toUtf8(game.path.stem()));
    // Les fichiers de codes portent le nom officiel du jeu (trouvé par le
    // scraper), sinon on tente avec le nom du fichier.
    const GameInfo* info = find(game.path);
    job.query = info && !info->name.empty() ? info->name : toUtf8(game.path.stem());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::erase_if(m_queue, [](const Job& j) { return j.kind == Job::Kind::CheatSearch; });
        m_queue.push_front(std::move(job)); // quelqu'un attend devant l'écran
    }
    m_cheats.clear();
    m_cheatNote.clear();
    m_cheatBusy = true;
    m_wake.notify_one();
}

void Scraper::searchCheatsJob(const Job& job)
{
    Cache& cache = *m_cache;
    cache.networkError = false;
    std::vector<std::string> names = {job.query, thumbnailName(job.query)};
    for (const std::string& system : systemNames(job.systemId, lowerExtension(job.path))) {
        for (const std::string& name : names) {
            const fs::path file = m_dir / "cheats" / fromUtf8(system) / fromUtf8(name + ".cht");
            const std::string url = kCheatsUrl + net::urlEncode(system) + "/" + net::urlEncode(name) + ".cht";
            if (cache.fetch(url, file, kRefreshDays, m_stop)) {
                std::vector<Cheat> cheats = parseCht(readFile(file));
                if (!cheats.empty()) {
                    post({.kind = Result::Kind::Cheats,
                          .message = std::to_string(cheats.size()) + " codes pour « " + name + " » (base libretro)",
                          .cheats = std::move(cheats)});
                    return;
                }
            }
        }
    }
    post({.kind = Result::Kind::Cheats,
          .message = cache.networkError ? "Pas de connexion : impossible de chercher des codes"
                                        : "Aucun code connu pour « " + job.query + " » dans la base libretro"});
}

void Scraper::searchCovers(const Game& game, const std::string& query)
{
    Job job;
    job.kind = Job::Kind::CoverSearch;
    job.systemId = game.system->id;
    job.path = game.path;
    job.region = game.region;
    job.query = query;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job.searchId = ++m_searchId;
        // Une recherche de jaquettes passe devant les jeux en attente :
        // quelqu'un attend le résultat devant l'écran.
        std::erase_if(m_queue, [](const Job& j) { return j.kind == Job::Kind::CoverSearch; });
        m_queue.push_front(std::move(job));
    }
    m_choices.clear();
    m_searchBusy = true;
    m_wake.notify_one();
}

void Scraper::forget(const Game& game)
{
    if (m_infos.erase(UserData::keyOf(game.path)))
        save();
}

bool Scraper::update()
{
    std::vector<Result> results;
    int latestSearch = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        results.swap(m_results);
        latestSearch = m_searchId;
    }
    bool changed = false;
    bool dirty = false;
    for (Result& r : results) {
        switch (r.kind) {
        case Result::Kind::Game:
            m_infos[r.key] = std::move(r.info);
            changed = dirty = true;
            break;
        case Result::Kind::Cover:
            if (r.searchId == latestSearch) {
                m_choices.push_back(std::move(r.choice));
                changed = true;
            }
            break;
        case Result::Kind::SearchDone:
            if (r.searchId == latestSearch)
                m_searchBusy = false;
            break;
        case Result::Kind::Message:
            m_messages.push_back(std::move(r.message));
            break;
        case Result::Kind::Cheats:
            m_cheats = std::move(r.cheats);
            m_cheatNote = std::move(r.message);
            m_cheatBusy = false;
            changed = true;
            break;
        }
    }
    if (dirty)
        save();
    return changed;
}

std::vector<std::string> Scraper::takeMessages()
{
    std::vector<std::string> out;
    out.swap(m_messages);
    return out;
}

// --- Fichier games.txt ----------------------------------------------------
// Une ligne par jeu, champs séparés par des tabulations :
//   <jeu> <trouvé 0/1> <par CRC 0/1> <nom> <genre> <développeur> <éditeur>
//   <année> <joueurs> <jaquette> <capture> <écran-titre> <date> <base>
// Les images sont notées par rapport au dossier du scraper.

void Scraper::load()
{
    std::ifstream in(m_dir / "games.txt");
    std::string line;
    auto image = [&](const std::string& relative) {
        return relative.empty() ? std::string() : toUtf8(m_dir / fromUtf8(relative));
    };
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::istringstream s(line);
        std::string field;
        while (std::getline(s, field, '\t'))
            f.push_back(field);
        if (f.size() < 13)
            continue;
        GameInfo info;
        info.status = f[1] == "1" ? GameInfo::Status::Found : GameInfo::Status::NotFound;
        info.byCrc = f[2] == "1";
        info.name = f[3];
        info.genre = f[4];
        info.developer = f[5];
        info.publisher = f[6];
        info.year = std::atoi(f[7].c_str());
        info.players = std::atoi(f[8].c_str());
        info.boxart = image(f[9]);
        info.snap = image(f[10]);
        info.titleScreen = image(f[11]);
        info.time = std::atoll(f[12].c_str());
        if (f.size() >= 14)
            info.database = f[13];
        else if (!info.boxart.empty()) // ancien fichier : déduite du dossier images/<base>/<type>/
            info.database = toUtf8(fromUtf8(info.boxart).parent_path().parent_path().filename());
        m_infos[portable::load(f[0], portable::baseOf(m_dir.parent_path()))] = std::move(info);
    }
}

void Scraper::save() const
{
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    std::ofstream out(m_dir / "games.txt", std::ios::trunc);
    auto relative = [&](const std::string& file) {
        return file.empty() ? std::string() : toUtf8(fromUtf8(file).lexically_relative(m_dir));
    };
    for (const auto& [key, i] : m_infos) {
        out << portable::store(key, portable::baseOf(m_dir.parent_path())) << '\t' << (i.status == GameInfo::Status::Found) << '\t' << i.byCrc << '\t' << i.name << '\t'
            << i.genre << '\t' << i.developer << '\t' << i.publisher << '\t' << i.year << '\t' << i.players << '\t'
            << relative(i.boxart) << '\t' << relative(i.snap) << '\t' << relative(i.titleScreen) << '\t' << i.time
            << '\t' << i.database << '\n';
    }
}

// --- Thread d'arrière-plan ------------------------------------------------

void Scraper::post(Result result)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results.push_back(std::move(result));
}

void Scraper::worker()
{
    bool warnedOffline = false;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_working = false;
            m_current.clear();
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                return;
            job = std::move(m_queue.front());
            m_queue.pop_front();
            m_working = job.kind == Job::Kind::Game;
            m_current = job.title;
        }

        if (job.kind == Job::Kind::CoverSearch) {
            searchCoversJob(job);
            continue;
        }
        if (job.kind == Job::Kind::CheatSearch) {
            searchCheatsJob(job);
            continue;
        }

        const auto start = std::chrono::steady_clock::now();
        GameInfo info = scrapeGame(job);
        if (m_stop)
            return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        SDL_Log("Scraper : %s -> %s%s (%lld ms)", job.title.c_str(), info.name.empty() ? "?" : info.name.c_str(),
                info.byCrc ? " [CRC]" : "", static_cast<long long>(ms.count()));

        // Sans réseau, on ne note pas "introuvable" : on réessaiera plus tard.
        if (info.status == GameInfo::Status::NotFound && m_cache->networkError) {
            if (!warnedOffline) {
                warnedOffline = true;
                post({.kind = Result::Kind::Message, .message = "Pas de connexion : jaquettes et informations non téléchargées"});
            }
            continue;
        }
        post({.kind = Result::Kind::Game, .key = job.key, .info = std::move(info)});
    }
}

GameInfo Scraper::scrapeGame(const Job& job)
{
    Cache& cache = *m_cache;
    GameInfo info;
    info.time = now();
    const std::vector<std::string> systems = systemNames(job.systemId, lowerExtension(job.path));
    cache.networkError = false;
    auto database = [&](const std::string& system) { return cache.database(system, m_dir, m_stop); };

    // 1. Reconnaître le jeu : par son CRC, sinon par son nom.
    const DbGame* entry = nullptr;
    std::string entrySystem;
    uint32_t crc = 0;
    const bool haveCrc = isCartridge(job.systemId) && job.size <= kMaxCrcSize && fileCrc(job.path, job.systemId, crc, m_stop);
    if (haveCrc) {
        for (const std::string& system : systems) {
            if (const GameDatabase* db = database(system); db && (entry = db->byCrc(crc))) {
                entrySystem = system;
                info.byCrc = true;
                break;
            }
        }
    }
    // Le nom exact du fichier : indispensable pour l'arcade ("sf2.zip"),
    // et sûr aussi pour les fichiers qui portent leur nom officiel.
    if (!entry) {
        const std::string fileName = toUtf8(job.path.filename());
        for (const std::string& system : systems) {
            if (const GameDatabase* db = database(system); db && (entry = db->byFileName(fileName))) {
                entrySystem = system;
                info.byCrc = true; // reconnu avec certitude
                break;
            }
        }
    }
    const bool arcade = job.systemId == "arcade" || job.systemId == "neogeo";
    if (!entry && !arcade) { // "sf2" ne veut rien dire comme titre : pas de recherche par nom
        for (const std::string& system : systems) {
            const GameDatabase* db = database(system);
            if (!db)
                continue;
            entry = db->byTitle(job.fileTitle, job.region);
            if (!entry && job.title != job.fileTitle)
                entry = db->byTitle(job.title, job.region);
            if (entry) {
                entrySystem = system;
                break;
            }
        }
    }
    if (entry) {
        info.database = entrySystem;
        info.name = entry->name;
        info.genre = entry->genre;
        info.developer = entry->developer;
        info.publisher = entry->publisher;
        info.year = entry->year;
        info.players = entry->players;
    }

    // 2. Les images, en commençant par la console où le jeu a été reconnu.
    std::vector<std::string> order = systems;
    if (!entrySystem.empty()) {
        std::erase(order, entrySystem);
        order.insert(order.begin(), entrySystem);
    }
    for (const std::string& system : order) {
        const Listing* listing = cache.listing(system, m_dir, m_stop);
        if (!listing)
            continue;

        std::string name;
        if (entry && listing->names.count(thumbnailName(entry->name))) {
            name = thumbnailName(entry->name); // le nom officiel exact : idéal
        } else {
            std::optional<size_t> i;
            if (entry)
                i = listing->index.find(thumbnailName(entry->name), job.region);
            if (!i)
                i = listing->index.find(job.fileTitle, job.region);
            if (!i && job.title != job.fileTitle)
                i = listing->index.find(job.title, job.region);
            if (i)
                name = listing->index.name(*i);
        }
        if (name.empty())
            continue;

        // Les trois sortes d'images portent le même nom.
        auto download = [&](const char* type) -> std::string {
            const fs::path file = imageFile(m_dir, system, type, name);
            const std::string url = kThumbnailsUrl + net::urlEncode(system) + "/" + type + "/" + net::urlEncode(name) + ".png";
            return cache.fetch(url, file, 0, m_stop) ? toUtf8(file) : std::string();
        };
        info.boxart = download("Named_Boxarts");
        info.snap = download("Named_Snaps");
        info.titleScreen = download("Named_Titles");
        if (info.name.empty())
            info.name = name;
        break;
    }

    info.status = (entry || !info.boxart.empty()) ? GameInfo::Status::Found : GameInfo::Status::NotFound;
    return info;
}

void Scraper::searchCoversJob(const Job& job)
{
    Cache& cache = *m_cache;
    auto stale = [&] {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stop || job.searchId != m_searchId;
    };
    size_t found = 0;
    for (const std::string& system : systemNames(job.systemId, lowerExtension(job.path))) {
        const Listing* listing = cache.listing(system, m_dir, m_stop);
        if (!listing)
            continue;
        for (size_t i : listing->index.search(job.query, job.region, kMaxChoices - found)) {
            if (stale())
                return;
            const std::string& name = listing->index.name(i);
            const fs::path file = imageFile(m_dir, system, "Named_Boxarts", name);
            const std::string url = kThumbnailsUrl + net::urlEncode(system) + "/Named_Boxarts/" + net::urlEncode(name) + ".png";
            if (cache.fetch(url, file, 0, m_stop)) {
                post({.kind = Result::Kind::Cover, .choice = {name, toUtf8(file)}, .searchId = job.searchId});
                ++found;
            }
        }
        if (found >= kMaxChoices)
            break;
    }
    post({.kind = Result::Kind::SearchDone, .searchId = job.searchId});
}

std::string boxartUrl(const GameInfo& info)
{
    // Le fichier est rangé comme sur le serveur : images/<base>/Named_Boxarts/<nom>.png
    // (les noms trop longs sont raccourcis sur le disque : on ne sait plus les retrouver).
    if (info.boxart.empty())
        return {};
    const fs::path file = fromUtf8(info.boxart);
    const std::string name = toUtf8(file.stem());
    const std::string system = toUtf8(file.parent_path().parent_path().filename());
    if (name.size() > 80 || system.empty())
        return {};
    return kThumbnailsUrl + net::urlEncode(system) + "/Named_Boxarts/" + net::urlEncode(name) + ".png";
}

} // namespace scrape
