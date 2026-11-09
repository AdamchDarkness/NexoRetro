#include "Achievements.hpp"

#include "Archive.hpp"
#include "Core.hpp"
#include "Utf8.hpp"
#include "net/Http.hpp"

#include <rc_client.h>
#include <rc_consoles.h>
#include <rc_libretro.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// rcheevos appelle des fonctions C (sans objet) : elles retrouvent l'unique
// instance par ces pointeurs.
Achievements* s_instance = nullptr;
Core* s_core = nullptr;

// La console du jeu, dans la numérotation de RetroAchievements.
uint32_t consoleOf(const std::string& systemId, const fs::path& rom)
{
    std::string ext = toUtf8(rom.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    struct Pair {
        const char* id;
        uint32_t console;
    };
    static constexpr Pair kConsoles[] = {
        {"nes", RC_CONSOLE_NINTENDO},          {"snes", RC_CONSOLE_SUPER_NINTENDO}, {"n64", RC_CONSOLE_NINTENDO_64},
        {"gb", RC_CONSOLE_GAMEBOY},            {"gbc", RC_CONSOLE_GAMEBOY_COLOR},   {"gba", RC_CONSOLE_GAMEBOY_ADVANCE},
        {"nds", RC_CONSOLE_NINTENDO_DS},       {"gc", RC_CONSOLE_GAMECUBE},         {"wii", RC_CONSOLE_WII},
        {"segacd", RC_CONSOLE_SEGA_CD},        {"saturn", RC_CONSOLE_SATURN},       {"a2600", RC_CONSOLE_ATARI_2600},
        {"a5200", RC_CONSOLE_ATARI_5200},      {"a7800", RC_CONSOLE_ATARI_7800},    {"lynx", RC_CONSOLE_ATARI_LYNX},
        {"jaguar", RC_CONSOLE_ATARI_JAGUAR},   {"ps1", RC_CONSOLE_PLAYSTATION},     {"ps2", RC_CONSOLE_PLAYSTATION_2},
        {"psp", RC_CONSOLE_PSP},               {"dc", RC_CONSOLE_DREAMCAST},        {"3ds", RC_CONSOLE_NINTENDO_3DS},
        {"arcade", RC_CONSOLE_ARCADE},         {"neogeo", RC_CONSOLE_ARCADE},
    };
    if (systemId == "nes" && ext == ".fds")
        return RC_CONSOLE_FAMICOM_DISK_SYSTEM;
    if (systemId == "md") {
        // Le dossier "Mega Drive" accueille aussi les autres consoles Sega à cartouche.
        if (ext == ".sms")
            return RC_CONSOLE_MASTER_SYSTEM;
        if (ext == ".gg")
            return RC_CONSOLE_GAME_GEAR;
        if (ext == ".32x")
            return RC_CONSOLE_SEGA_32X;
        if (ext == ".sg")
            return RC_CONSOLE_SG1000;
        return RC_CONSOLE_MEGA_DRIVE;
    }
    for (const Pair& p : kConsoles)
        if (systemId == p.id)
            return p.console;
    return RC_CONSOLE_UNKNOWN;
}

// rc_libretro demande les zones de mémoire simples du core (RAM, sauvegarde...).
void RC_CCONV coreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info)
{
    size_t size = 0;
    info->data = s_core ? static_cast<uint8_t*>(s_core->memoryData(id, size)) : nullptr;
    info->size = info->data ? size : 0;
}

void RC_CCONV logMessage(const char* message, const rc_client_t*)
{
    SDL_Log("Succès : %s", message);
}

rc_clock_t RC_CCONV clockMs(const rc_client_t*)
{
    return static_cast<rc_clock_t>(SDL_GetTicks());
}

// Un jeu dans une archive (.zip, .7z) : l'empreinte est celle du jeu, pas de
// l'archive. On le sort dans un dossier temporaire (sauf l'arcade, dont
// l'empreinte est le nom du fichier).
fs::path unpacked(const fs::path& rom, uint32_t console)
{
    std::string ext = toUtf8(rom.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if ((ext != ".zip" && ext != ".7z") || console == RC_CONSOLE_ARCADE)
        return rom;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "NexoRetro-succes";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (!archive::extract(rom, dir))
        return rom;
    // Le plus gros fichier de l'archive est le jeu.
    fs::path best;
    uintmax_t bestSize = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->file_size(ec) > bestSize) {
            bestSize = it->file_size(ec);
            best = it->path();
        }
    return best.empty() ? rom : best;
}

} // namespace

Achievements::Achievements()
{
    s_instance = this;
    m_client = rc_client_create(&Achievements::readMemory, &Achievements::serverCall);
    rc_client_enable_logging(m_client, RC_CLIENT_LOG_LEVEL_WARN, logMessage);
    rc_client_set_event_handler(m_client, &Achievements::onEvent);
    rc_client_set_get_time_millisecs_function(m_client, clockMs);
    // Le nom sous lequel Nexo Retro se présente au site : il doit être unique
    // et stable ("NexoRetro/0.6.1 (Windows) rcheevos/12.5"), comme le
    // demandent les règles de RetroAchievements.
    char clause[64] = "";
    rc_client_get_user_agent_clause(m_client, clause, sizeof(clause));
#ifdef __ANDROID__
    const char* os = "Android";
#else
    const char* os = "Windows";
#endif
    m_userAgent = std::string("NexoRetro/") + NEXO_VERSION + " (" + os + ") " + clause;
    m_thread = std::thread([this] { worker(); });
}

Achievements::~Achievements()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    unloadGame();
    rc_client_destroy(m_client);
    if (s_instance == this)
        s_instance = nullptr;
}

// --- Le réseau -------------------------------------------------------------------

void Achievements::serverCall(const rc_api_request_t* request, ServerCallback callback, void* data, rc_client_t*)
{
    Achievements& self = *s_instance;
    Request r{request->url, request->post_data ? request->post_data : "",
              request->content_type ? request->content_type : "application/x-www-form-urlencoded", {}, callback, data};
    {
        std::lock_guard<std::mutex> lock(self.m_mutex);
        self.m_requests.push_back(std::move(r));
    }
    self.m_wake.notify_one();
}

void Achievements::worker()
{
    net::HttpClient http(m_userAgent.c_str());
    for (;;) {
        Request r;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_requests.empty(); });
            if (m_stop)
                return;
            r = std::move(m_requests.front());
            m_requests.pop_front();
        }
        if (!r.saveTo.empty()) {
            // Une image de succès : écrite sous un nom provisoire, puis
            // renommée (l'interface ne lit jamais une image à moitié écrite).
            const net::HttpResponse image = http.get(r.url);
            if (image.ok() && !image.body.empty()) {
                std::error_code ec;
                fs::create_directories(r.saveTo.parent_path(), ec);
                fs::path part = r.saveTo;
                part += ".part";
                bool written = false;
                {
                    std::ofstream out(part, std::ios::binary);
                    out.write(image.body.data(), static_cast<std::streamsize>(image.body.size()));
                    written = static_cast<bool>(out);
                }
                if (written)
                    fs::rename(part, r.saveTo, ec);
            }
            continue;
        }
        // Les échanges avec le site se font en POST : le mot de passe (à la
        // connexion) et le jeton n'apparaissent jamais dans l'adresse.
        const net::HttpResponse answer = r.post.empty() ? http.get(r.url) : http.post(r.url, r.post, r.contentType);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_responses.push_back({answer.status, answer.body, r.callback, r.data});
    }
}

void Achievements::update()
{
    // Les réponses arrivées, rendues à rcheevos sur le fil principal.
    std::deque<Response> ready;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ready.swap(m_responses);
    }
    for (Response& r : ready) {
        rc_api_server_response_t response{};
        response.body = r.body.c_str();
        response.body_length = r.body.size();
        // 0 = pas de réponse : rcheevos réessaiera plus tard (déblocages gardés en attente).
        response.http_status_code = r.status ? r.status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        r.callback(&response, r.data);
    }
    // Jeu en pause (menu ouvert...) : rcheevos continue de dire au site que
    // la partie est en cours, et renvoie les déblocages en attente.
    if (!m_framed)
        rc_client_idle(m_client);
    m_framed = false;
}

std::string Achievements::badgeFile(const char* url) const
{
    if (!url || !*url || m_badgeDir.empty())
        return {};
    // ".../Badge/12345.png" -> <données>/achievements/12345.png
    std::string name = url;
    name = name.substr(name.find_last_of('/') + 1);
    if (name.empty() || name.find("..") != std::string::npos)
        return {};
    const fs::path file = m_badgeDir / fromUtf8(name);
    std::error_code ec;
    if (!fs::exists(file, ec) && std::find(m_badgesAsked.begin(), m_badgesAsked.end(), name) == m_badgesAsked.end()) {
        m_badgesAsked.push_back(name);
        auto& self = const_cast<Achievements&>(*this);
        {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            self.m_requests.push_back({url, {}, {}, file, nullptr, nullptr});
        }
        self.m_wake.notify_one();
    }
    return toUtf8(file);
}

// --- Le compte ------------------------------------------------------------------

void Achievements::loginWithPassword(const std::string& user, const std::string& password)
{
    m_login = Login::LoggingIn;
    m_loginError.clear();
    rc_client_begin_login_with_password(
        m_client, user.c_str(), password.c_str(),
        [](int result, const char* error, rc_client_t* client, void*) {
            Achievements& self = *s_instance;
            if (result == RC_OK) {
                self.m_login = Login::LoggedIn;
                // Le jeton remplace le mot de passe, qu'on oublie aussitôt.
                if (const rc_client_user_t* u = rc_client_get_user_info(client)) {
                    self.m_newUser = u->username;
                    self.m_newToken = u->token;
                }
            } else {
                self.m_login = Login::Failed;
                self.m_loginError = error ? error : "connexion impossible";
            }
        },
        nullptr);
}

void Achievements::loginWithToken(const std::string& user, const std::string& token)
{
    if (user.empty() || token.empty())
        return;
    m_login = Login::LoggingIn;
    m_loginError.clear();
    rc_client_begin_login_with_token(
        m_client, user.c_str(), token.c_str(),
        [](int result, const char* error, rc_client_t*, void*) {
            Achievements& self = *s_instance;
            self.m_login = result == RC_OK ? Login::LoggedIn : Login::Failed;
            if (result != RC_OK)
                self.m_loginError = error ? error : "connexion impossible";
        },
        nullptr);
}

void Achievements::logout()
{
    rc_client_logout(m_client);
    m_login = Login::LoggedOut;
    m_loginError.clear();
}

std::string Achievements::userName() const
{
    const rc_client_user_t* u = rc_client_get_user_info(m_client);
    return u && u->display_name ? u->display_name : "";
}

int Achievements::score() const
{
    const rc_client_user_t* u = rc_client_get_user_info(m_client);
    if (!u)
        return 0;
    return static_cast<int>(rc_client_get_hardcore_enabled(m_client) ? u->score : u->score_softcore);
}

bool Achievements::takeNewToken(std::string& user, std::string& token)
{
    if (m_newToken.empty())
        return false;
    user = std::move(m_newUser);
    token = std::move(m_newToken);
    m_newUser.clear();
    m_newToken.clear();
    return true;
}

void Achievements::setHardcore(bool on)
{
    rc_client_set_hardcore_enabled(m_client, on ? 1 : 0);
}

bool Achievements::hardcore() const
{
    return rc_client_get_hardcore_enabled(m_client) != 0;
}

// --- La partie ------------------------------------------------------------------

void Achievements::loadGame(Core& core, const std::string& systemId, const fs::path& romPath)
{
    unloadGame();
    if (!m_enabled || (m_login != Login::LoggedIn && m_login != Login::LoggingIn) || romPath.empty())
        return;
    const uint32_t console = consoleOf(systemId, romPath);
    if (console == RC_CONSOLE_UNKNOWN)
        return;
    s_core = &core;
    // La carte de la mémoire de la console, telle que le core l'a décrite.
    auto* regions = new rc_libretro_memory_regions_t{};
    if (!rc_libretro_memory_init(regions, core.memoryMap(), coreMemoryInfo, console))
        SDL_Log("Succès : mémoire du jeu introuvable (ce core ne la montre pas)");
    m_memory = regions;
    SDL_Log("Succès : mémoire du jeu : %u zone(s), %u Ko", regions->count, static_cast<unsigned>(regions->total_size / 1024));
    m_gameLoading = true;
    const std::string path = toUtf8(unpacked(romPath, console));
    rc_client_begin_identify_and_load_game(
        m_client, console, path.c_str(), nullptr, 0,
        [](int result, const char* error, rc_client_t* client, void*) {
            Achievements& self = *s_instance;
            self.m_gameLoading = false;
            if (result == RC_NO_GAME_LOADED || result == RC_HARDCORE_DISABLED)
                return; // jeu inconnu du site : rien à signaler
            if (result != RC_OK) {
                SDL_Log("Succès : %s", error ? error : "jeu non chargé");
                return;
            }
            const rc_client_game_t* game = rc_client_get_game_info(client);
            rc_client_user_game_summary_t s{};
            rc_client_get_user_game_summary(client, &s);
            Notice n;
            n.title = game && game->title ? game->title : "";
            if (s.num_core_achievements == 0)
                n.text = "Pas encore de succès pour ce jeu";
            else
                n.text = std::to_string(s.num_unlocked_achievements) + " / " + std::to_string(s.num_core_achievements) +
                         " succès débloqués" + (rc_client_get_hardcore_enabled(client) ? " · Hardcore" : "");
            if (game) {
                char url[256] = "";
                if (rc_client_game_get_image_url(game, url, sizeof(url)) == RC_OK)
                    n.badge = self.badgeFile(url);
            }
            self.notice(std::move(n));
        },
        nullptr);
}

void Achievements::unloadGame()
{
    rc_client_unload_game(m_client);
    if (m_memory) {
        auto* regions = static_cast<rc_libretro_memory_regions_t*>(m_memory);
        rc_libretro_memory_destroy(regions);
        delete regions;
        m_memory = nullptr;
    }
    s_core = nullptr;
    m_gameLoading = false;
}

bool Achievements::active() const
{
    return rc_client_is_game_loaded(m_client) != 0;
}

uint32_t Achievements::readMemory(uint32_t address, uint8_t* buffer, uint32_t size, rc_client_t*)
{
    const auto* regions = s_instance ? static_cast<const rc_libretro_memory_regions_t*>(s_instance->m_memory) : nullptr;
    return regions ? rc_libretro_memory_read(regions, address, buffer, size) : 0;
}

void Achievements::doFrame()
{
    if (m_memory) {
        rc_client_do_frame(m_client);
        m_framed = true;
    }
}

void Achievements::reset()
{
    rc_client_reset(m_client);
}

void Achievements::stateLoaded()
{
    // La sauvegarde d'état ne contient pas l'avancement des succès : on le
    // remet à zéro (sinon un succès "en 3 étapes" se croirait déjà à moitié fait).
    rc_client_deserialize_progress_sized(m_client, nullptr, 0);
}

bool Achievements::takeResetRequest()
{
    const bool r = m_resetRequested;
    m_resetRequested = false;
    return r;
}

void Achievements::notice(Notice n)
{
    m_notices.push_back(std::move(n));
}

std::vector<Achievements::Notice> Achievements::takeNotices()
{
    std::vector<Notice> out;
    out.swap(m_notices);
    return out;
}

void Achievements::onEvent(const rc_client_event_t* event, rc_client_t* client)
{
    Achievements& self = *s_instance;
    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        const rc_client_achievement_t* a = event->achievement;
        Notice n;
        n.kind = Notice::Kind::Unlocked;
        n.title = a->title ? a->title : "";
        n.text = a->description ? a->description : "";
        n.points = static_cast<int>(a->points);
        char url[256] = "";
        if (rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url)) == RC_OK)
            n.badge = self.badgeFile(url);
        SDL_Log("Succès débloqué : %s (%u points)", n.title.c_str(), a->points);
        self.notice(std::move(n));
        break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED: {
        const rc_client_game_t* game = rc_client_get_game_info(client);
        Notice n;
        n.kind = Notice::Kind::Mastered;
        n.title = game && game->title ? game->title : "";
        n.text = rc_client_get_hardcore_enabled(client) ? "Jeu maîtrisé : tous les succès en Hardcore !"
                                                        : "Jeu terminé : tous les succès débloqués !";
        self.notice(std::move(n));
        break;
    }
    case RC_CLIENT_EVENT_RESET:
        // Le Hardcore vient d'être activé : la partie doit repartir de zéro.
        self.m_resetRequested = true;
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (event->server_error && event->server_error->error_message)
            self.notice({Notice::Kind::Error, "RetroAchievements", event->server_error->error_message, {}, 0});
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        self.notice({Notice::Kind::Error, "RetroAchievements", "Hors ligne : les succès seront envoyés plus tard", {}, 0});
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        self.notice({Notice::Kind::Info, "RetroAchievements", "Reconnecté : succès envoyés", {}, 0});
        break;
    default:
        break; // classements, indicateurs de défi... : pas encore affichés
    }
}

// --- Pour l'interface -----------------------------------------------------------

Achievements::GameSummary Achievements::summary() const
{
    GameSummary out;
    const rc_client_game_t* game = rc_client_get_game_info(m_client);
    if (!game || !rc_client_is_game_loaded(m_client))
        return out;
    rc_client_user_game_summary_t s{};
    rc_client_get_user_game_summary(m_client, &s);
    out.loaded = true;
    out.title = game->title ? game->title : "";
    out.total = static_cast<int>(s.num_core_achievements);
    out.unlocked = static_cast<int>(s.num_unlocked_achievements);
    out.points = static_cast<int>(s.points_core);
    out.pointsUnlocked = static_cast<int>(s.points_unlocked);
    return out;
}

std::vector<Achievements::Item> Achievements::items() const
{
    std::vector<Item> out;
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(
        m_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
    if (!list)
        return out;
    for (uint32_t b = 0; b < list->num_buckets; ++b) {
        const rc_client_achievement_bucket_t& bucket = list->buckets[b];
        for (uint32_t i = 0; i < bucket.num_achievements; ++i) {
            const rc_client_achievement_t* a = bucket.achievements[i];
            Item item;
            item.title = a->title ? a->title : "";
            item.description = a->description ? a->description : "";
            item.progress = a->measured_progress;
            item.percent = a->measured_percent;
            item.points = static_cast<int>(a->points);
            item.unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE &&
                            (!rc_client_get_hardcore_enabled(m_client) || (a->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE));
            item.unsupported = bucket.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED;
            item.badge = badgeFile(item.unlocked ? a->badge_url : a->badge_locked_url);
            out.push_back(std::move(item));
        }
    }
    rc_client_destroy_achievement_list(list);
    return out;
}

std::string Achievements::richPresence() const
{
    char buffer[256] = "";
    if (!rc_client_has_rich_presence(m_client))
        return {};
    rc_client_get_rich_presence_message(m_client, buffer, sizeof(buffer));
    return buffer;
}
