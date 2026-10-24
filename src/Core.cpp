#include "Core.hpp"

#include "Audio.hpp"
#include "Hash.hpp"
#include "Input.hpp"
#include "Utf8.hpp"
#include "Video.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <set>
#include <type_traits>
#include <vector>

// [C++] Un namespace anonyme joue le rôle du "static" du C pour les fonctions
// et variables globales : elles ne sont visibles que dans ce fichier.
namespace {

// L'instance de Core actuellement branchée sur la DLL (voir Core.hpp).
Core* g_current = nullptr;

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    // Lit tout le fichier d'un coup dans un vector.
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool writeFile(const std::filesystem::path& path, const void* data, size_t size)
{
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

// Le core nous envoie ses messages (erreurs, BIOS manquant...) par ici.
// Les messages d'erreur du core, pour expliquer un échec à l'utilisateur.
std::string g_coreErrors;

void coreLog(retro_log_level level, const char* fmt, ...)
{
    if (level == RETRO_LOG_DEBUG)
        return;
    // Les messages d'information des cores sont très nombreux (PCSX2 : une
    // ligne par effet graphique compilé) et noient la console : seulement
    // avertissements et erreurs, sauf avec EMU_CORE_LOG=1 (pour déboguer).
    static const bool verbose = SDL_getenv("EMU_CORE_LOG") != nullptr;
    if (level == RETRO_LOG_INFO && !verbose)
        return;
    char text[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    static const char* const names[] = {"DEBUG", "INFO", "WARN", "ERREUR"};
    std::fprintf(stderr, "[core %s] %s", names[level <= RETRO_LOG_ERROR ? level : RETRO_LOG_ERROR], text);
    if (level == RETRO_LOG_ERROR && g_coreErrors.size() < 4096)
        g_coreErrors += text;
}

// Traduit les messages d'erreur du core en une phrase pour l'utilisateur.
std::string explainErrors(const std::string& errors)
{
    // MAME / FBNeo : "xxx.bin NOT FOUND" pour chaque fichier absent du zip.
    std::vector<std::string> missing;
    for (size_t pos = 0; (pos = errors.find(" NOT FOUND", pos)) != std::string::npos; pos += 10) {
        const size_t start = errors.find_last_of(" \n", pos - 1);
        const std::string name = errors.substr(start == std::string::npos ? 0 : start + 1,
                                               pos - (start == std::string::npos ? 0 : start + 1));
        if (!name.empty() && missing.size() < 3 && std::find(missing.begin(), missing.end(), name) == missing.end())
            missing.push_back(name);
    }
    if (!missing.empty()) {
        std::string list;
        for (const std::string& m : missing)
            list += (list.empty() ? "" : ", ") + m;
        return "fichiers absents du zip (" + list + ") : le romset ne correspond pas à la version de l'émulateur";
    }
    // Sinon : la dernière ligne d'erreur non vide.
    std::istringstream lines(errors);
    std::string line, last;
    while (std::getline(lines, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        if (!line.empty())
            last = line;
    }
    return last;
}

} // namespace

Core::Core(Video& video, Audio& audio, Input& input)
    // [C++] La "liste d'initialisation" : les membres sont construits ici,
    // avant même d'entrer dans le corps du constructeur. Obligatoire pour
    // les références, qui ne peuvent pas être assignées plus tard.
    : m_video(video), m_audio(audio), m_input(input)
{
}

Core::~Core()
{
    netpacketStop();
    unloadGame();
    if (m_initialized) {
        m_video.beginCore();
        m_api.deinit();
        m_video.endCore();
    }
    if (m_handle)
        SDL_UnloadObject(m_handle);
    if (g_current == this)
        g_current = nullptr;
}

bool Core::load(const std::filesystem::path& dllPath)
{
    m_handle = SDL_LoadObject(toUtf8(dllPath).c_str());
    if (!m_handle) {
        SDL_Log("Impossible de charger le core %s : %s", toUtf8(dllPath).c_str(), SDL_GetError());
        return false;
    }

    // [C++] Une "lambda" est une fonction locale qui peut utiliser les
    // variables autour d'elle ([&] = par référence). "auto& fn" en fait une
    // fonction générique : elle marche pour chaque type de pointeur de l'Api.
    bool ok = true;
    auto bind = [&](auto& fn, const char* name) {
        using FnType = std::remove_reference_t<decltype(fn)>;
        fn = reinterpret_cast<FnType>(SDL_LoadFunction(m_handle, name));
        if (!fn) {
            SDL_Log("Fonction absente du core : %s", name);
            ok = false;
        }
    };
    bind(m_api.init, "retro_init");
    bind(m_api.deinit, "retro_deinit");
    bind(m_api.apiVersion, "retro_api_version");
    bind(m_api.getSystemInfo, "retro_get_system_info");
    bind(m_api.getSystemAvInfo, "retro_get_system_av_info");
    bind(m_api.setEnvironment, "retro_set_environment");
    bind(m_api.setVideoRefresh, "retro_set_video_refresh");
    bind(m_api.setAudioSample, "retro_set_audio_sample");
    bind(m_api.setAudioSampleBatch, "retro_set_audio_sample_batch");
    bind(m_api.setInputPoll, "retro_set_input_poll");
    bind(m_api.setInputState, "retro_set_input_state");
    bind(m_api.setControllerPortDevice, "retro_set_controller_port_device");
    bind(m_api.reset, "retro_reset");
    bind(m_api.run, "retro_run");
    bind(m_api.serializeSize, "retro_serialize_size");
    bind(m_api.serialize, "retro_serialize");
    bind(m_api.unserialize, "retro_unserialize");
    bind(m_api.loadGame, "retro_load_game");
    bind(m_api.unloadGame, "retro_unload_game");
    bind(m_api.getMemoryData, "retro_get_memory_data");
    bind(m_api.getMemorySize, "retro_get_memory_size");
    bind(m_api.cheatReset, "retro_cheat_reset");
    bind(m_api.cheatSet, "retro_cheat_set");
    if (!ok)
        return false;

    if (m_api.apiVersion() != RETRO_API_VERSION) {
        SDL_Log("Version d'API libretro incompatible : %u", m_api.apiVersion());
        return false;
    }

    // L'ordre compte : l'environnement AVANT retro_init(), les autres
    // fonctions (image, son, manettes) APRÈS, comme le fait RetroArch.
    // Certains cores (Mesen, pour la NES) s'en servent dès qu'on les leur
    // donne, sur des objets qu'ils ne créent que dans retro_init : les donner
    // avant les faisait planter au chargement.
    g_current = this;
    m_api.setEnvironment(&Core::onEnvironment);
    m_api.init();
    m_initialized = true;
    m_api.setVideoRefresh(&Core::onVideoRefresh);
    m_api.setAudioSample(&Core::onAudioSample);
    m_api.setAudioSampleBatch(&Core::onAudioSampleBatch);
    m_api.setInputPoll(&Core::onInputPoll);
    m_api.setInputState(&Core::onInputState);

    m_api.getSystemInfo(&m_info);
    SDL_Log("Core chargé : %s %s (extensions : %s)", m_info.library_name, m_info.library_version,
            m_info.valid_extensions ? m_info.valid_extensions : "?");
    return true;
}

bool Core::loadGame(const std::filesystem::path& romPath)
{
    g_coreErrors.clear();
    m_loadError.clear();
    retro_game_info game{};
    retro_game_info* gamePtr = nullptr;

    if (!romPath.empty()) {
        // Les cores libretro attendent des chemins en UTF-8.
        m_romPath = toUtf8(romPath);
        game.path = m_romPath.c_str();
        // Certains cores (souvent ceux des consoles à CD) veulent juste le
        // chemin et lisent le fichier eux-mêmes. Les autres veulent les octets.
        if (!m_info.need_fullpath) {
            m_romData = readFile(romPath);
            if (m_romData.empty()) {
                SDL_Log("Impossible de lire le jeu : %s", m_romPath.c_str());
                return false;
            }
            game.data = m_romData.data();
            game.size = m_romData.size();
        }
        gamePtr = &game;
    } else if (!m_supportsNoGame) {
        SDL_Log("Ce core a besoin d'un jeu pour démarrer.");
        return false;
    }

    if (!m_api.loadGame(gamePtr)) {
        SDL_Log("Le core a refusé le jeu (BIOS manquant ? format non supporté ?)");
        m_loadError = explainErrors(g_coreErrors);
        return false;
    }
    m_gameLoaded = true;

    m_api.getSystemAvInfo(&m_avInfo);
    if (!m_video.createContext(m_avInfo.geometry))
        return false;
    applyAvInfo();
    m_api.setControllerPortDevice(0, RETRO_DEVICE_JOYPAD);
    m_api.setControllerPortDevice(1, RETRO_DEVICE_JOYPAD);
    return true;
}

void Core::runFrame()
{
    // Le core dessine dans son propre contexte OpenGL (voir Video::beginCore).
    if (m_audioStatus)
        m_audioStatus(true, m_audio.occupancy(), m_audio.underrunLikely());
    m_video.beginCore();
    m_api.run();
    m_video.endCore();
}

void Core::unloadGame()
{
    if (!m_gameLoaded)
        return;
    // Le core libère ses ressources OpenGL tant que le contexte existe encore.
    m_video.destroyHwContext();
    m_video.beginCore();
    m_api.unloadGame();
    m_video.endCore();
    m_gameLoaded = false;
    m_romData.clear();
}

void Core::applyAvInfo()
{
    SDL_Log("Vidéo %ux%u à %.3f images/s, audio %.0f Hz", m_avInfo.geometry.base_width,
            m_avInfo.geometry.base_height, m_avInfo.timing.fps, m_avInfo.timing.sample_rate);
    m_video.setGeometry(m_avInfo.geometry);
    m_audio.open(m_avInfo.timing.sample_rate);
}

bool Core::saveState(const std::filesystem::path& file) const
{
    const size_t size = m_api.serializeSize();
    if (size == 0)
        return false;
    std::vector<uint8_t> buffer(size);
    // (Certains cores lisent la mémoire de la carte graphique pour leurs
    // sauvegardes d'état : dans leur contexte OpenGL.)
    m_video.beginCore();
    const bool ok = m_api.serialize(buffer.data(), size);
    m_video.endCore();
    if (!ok)
        return false;
    return writeFile(file, buffer.data(), buffer.size());
}

bool Core::loadState(const std::filesystem::path& file)
{
    const std::vector<uint8_t> buffer = readFile(file);
    if (buffer.empty())
        return false;
    m_video.beginCore();
    const bool ok = m_api.unserialize(buffer.data(), buffer.size());
    m_video.endCore();
    return ok;
}

bool Core::serialize(std::string& out) const
{
    const size_t size = m_api.serializeSize();
    if (size == 0) {
        SDL_Log("Sauvegarde d'état impossible : le core annonce une taille nulle");
        return false;
    }
    out.assign(size, '\0');
    m_video.beginCore();
    const bool ok = m_api.serialize(out.data(), size);
    m_video.endCore();
    if (!ok) {
        SDL_Log("Sauvegarde d'état impossible : le core a refusé (%zu octets prévus)", size);
        return false;
    }
    return true;
}

bool Core::unserialize(const std::string& data)
{
    if (data.empty())
        return false;
    m_video.beginCore();
    const bool ok = m_api.unserialize(data.data(), data.size());
    m_video.endCore();
    return ok;
}

bool Core::rollbackSafe() const
{
    // Le core nous a prévenus que ses états sont incomplets, ou ne peuvent
    // pas être rechargés dans une autre session.
    if (m_serializationQuirks & (RETRO_SERIALIZATION_QUIRK_INCOMPLETE | RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION))
        return false;
    // Cas connus qui ne le signalent pas : leur sauvegarde d'état ne remet
    // pas exactement le jeu dans l'état d'avant (vérifié dans leur code).
    const std::string name = m_info.library_name ? m_info.library_name : "";
    for (const char* unsafe : {"2048", "Mr.Boom"})
        if (name == unsafe)
            return false;
    return true;
}

std::string Core::coreInfo() const
{
    return std::string(m_info.library_name ? m_info.library_name : "?") + " " +
           (m_info.library_version ? m_info.library_version : "?");
}

uint64_t Core::memoryFingerprint() const
{
    if (!m_api.getMemoryData || !m_api.getMemorySize)
        return 0;
    Hasher64 hasher;
    bool any = false;
    for (unsigned id : {RETRO_MEMORY_SYSTEM_RAM, RETRO_MEMORY_SAVE_RAM}) {
        const void* data = m_api.getMemoryData(id);
        const size_t size = m_api.getMemorySize(id);
        if (data && size) {
            hasher.update(data, size);
            any = true;
        }
    }
    return any ? hasher.finish() | 1 : 0; // jamais 0 quand elle existe
}

bool Core::saveSram(const std::filesystem::path& file) const
{
    const void* data = m_api.getMemoryData(RETRO_MEMORY_SAVE_RAM);
    const size_t size = m_api.getMemorySize(RETRO_MEMORY_SAVE_RAM);
    if (!data || size == 0)
        return false; // ce jeu n'a pas de sauvegarde interne
    return writeFile(file, data, size);
}

bool Core::loadSram(const std::filesystem::path& file)
{
    void* data = m_api.getMemoryData(RETRO_MEMORY_SAVE_RAM);
    const size_t size = m_api.getMemorySize(RETRO_MEMORY_SAVE_RAM);
    const std::vector<uint8_t> buffer = readFile(file);
    if (!data || size == 0 || buffer.empty())
        return false;
    std::memcpy(data, buffer.data(), buffer.size() < size ? buffer.size() : size);
    SDL_Log("Sauvegarde chargée : %s", toUtf8(file).c_str());
    return true;
}

// --- Callbacks appelés par le core -----------------------------------------

// Le core nous pose des questions ("où sont les BIOS ?") ou nous informe
// ("mes pixels sont en XRGB8888"). On répond true si on a compris la demande.
bool Core::onEnvironment(unsigned cmd, void* data)
{
    Core& self = *g_current;
    // EMU_ENV_TRACE=1 : chaque demande du core, dans l'ordre (pour trouver
    // celle qui précède un plantage). Écrit tout de suite, sans tampon.
    if (static const bool trace = SDL_getenv("EMU_ENV_TRACE") != nullptr; trace) {
        std::fprintf(stderr, "[env] %u%s\n", cmd & 0xFFFF, (cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (exp)" : "");
        std::fflush(stderr);
    }
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = &coreLog;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        // "Si l'image n'a pas changé, tu peux m'envoyer NULL."
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return self.m_video.setPixelFormat(*static_cast<const retro_pixel_format*>(data));
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = self.m_systemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = self.m_saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        self.m_supportsNoGame = *static_cast<const bool*>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
        // Ce que fait chaque bouton dans CETTE console : "Wiimote 1",
        // "Shake", "C-Haut"... (la liste se termine par une description vide).
        // Le core la renvoie quand on change de type de manette.
        self.m_inputDescriptors.clear();
        for (auto* d = static_cast<const retro_input_descriptor*>(data); d && d->description; ++d)
            self.m_inputDescriptors.push_back({d->port, d->device, d->index, d->id, d->description});
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
        // Les types de manette que le core sait émuler, port par port
        // (Dolphin : Wiimote, Wiimote + Nunchuk, Classic Controller...).
        // La liste se termine par une entrée vide.
        self.m_controllerTypes.clear();
        for (auto* port = static_cast<const retro_controller_info*>(data); port && port->types; ++port) {
            std::vector<ControllerType> types;
            for (unsigned i = 0; i < port->num_types; ++i)
                types.push_back({port->types[i].desc ? port->types[i].desc : "?", port->types[i].id});
            self.m_controllerTypes.push_back(std::move(types));
        }
        if (!self.m_controllerTypes.empty()) {
            std::string list;
            for (const ControllerType& t : self.m_controllerTypes.front())
                list += (list.empty() ? "" : ", ") + t.name + " (" + std::to_string(t.id) + ")";
            SDL_Log("Manettes proposées par le core (port 1) : %s", list.c_str());
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        self.m_avInfo.geometry = *static_cast<const retro_game_geometry*>(data);
        self.m_video.setGeometry(self.m_avInfo.geometry);
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        self.m_avInfo = *static_cast<const retro_system_av_info*>(data);
        self.applyAvInfo();
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        // Les cores qui savent faire OpenGL et Vulkan nous demandent notre
        // préférence : celle de Paramètres > Affichage (OpenGL par défaut).
        *static_cast<unsigned*>(data) = self.m_preferVulkan ? RETRO_HW_CONTEXT_VULKAN : RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
        // "Quelle version de la négociation connais-tu ?" Vulkan : la 2.
        auto* query = static_cast<retro_hw_render_context_negotiation_interface*>(data);
        query->interface_version = query->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN
                                       ? RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION
                                       : 0;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
        // Le core veut choisir lui-même comment créer son device Vulkan.
        const auto* negotiation = static_cast<const retro_hw_render_context_negotiation_interface*>(data);
        if (!negotiation || negotiation->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
            return false;
        self.m_video.setVulkanNegotiation(
            reinterpret_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(negotiation));
        return true;
    }
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
        // Après context_reset : le core récupère son device Vulkan et nos
        // fonctions (set_image...).
        retro_hw_render_interface_vulkan* vulkan = self.m_video.vulkanInterface();
        if (!vulkan)
            return false;
        *static_cast<const retro_hw_render_interface**>(data) = reinterpret_cast<const retro_hw_render_interface*>(vulkan);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        // Le core veut dessiner lui-même avec la carte graphique (3D).
        return self.m_video.requestHwRender(*static_cast<retro_hw_render_callback*>(data));
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        // Le core décrit les limites de ses sauvegardes d'état (pour le
        // jeu en ligne) : on les retient.
        self.m_serializationQuirks = *static_cast<uint64_t*>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        // "À quoi va servir la sauvegarde d'état ?" Au jeu en ligne, elle
        // part sur un autre PC : le core doit la rendre portable.
        if (data)
            *static_cast<int*>(data) =
                self.m_netplay ? RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY : RETRO_SAVESTATE_CONTEXT_NORMAL;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        // "Dois-je produire l'image et le son de cette image ?" Pendant un
        // recalcul (rollback), personne ne verra l'image : on s'en passe.
        // Le son, lui, reste "activé" (on le coupe de notre côté) : certains
        // cores (mGBA) n'avancent plus leur puce sonore sans lui, et le jeu
        // diverge alors d'un PC à l'autre (désynchronisation vue en test).
        if (data)
            *static_cast<int*>(data) = self.m_skipAudioVideo ? RETRO_AV_ENABLE_AUDIO : (RETRO_AV_ENABLE_VIDEO | RETRO_AV_ENABLE_AUDIO);
        return true;
    // --- Options du core ---------------------------------------------------
    // Sans réponse ici, un core garde des valeurs "vides" qui ne sont pas
    // forcément ses valeurs par défaut (MAME confondait alors le dossier
    // du jeu avec un nom de console).
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2; // on comprend les options "version 2"
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        // Ancienne forme : "Description; valeur1|valeur2|..." (la 1re est le défaut).
        for (auto* v = static_cast<const retro_variable*>(data); v && v->key; ++v) {
            CoreOption o;
            o.key = v->key;
            const std::string text = v->value ? v->value : "";
            const size_t semi = text.find(';');
            o.label = text.substr(0, semi);
            if (semi != std::string::npos) {
                std::istringstream values(text.substr(semi + 1));
                std::string value;
                while (std::getline(values, value, '|')) {
                    value.erase(0, value.find_first_not_of(' '));
                    o.values.emplace_back(value, value);
                }
            }
            o.defaultValue = o.values.empty() ? std::string() : o.values.front().first;
            self.defineOption(std::move(o));
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        self.defineOptions(static_cast<const retro_core_option_definition*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        self.defineOptions(static_cast<const retro_core_options_intl*>(data)->us);
        self.translateOptions(static_cast<const retro_core_options_intl*>(data)->local);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        if (const auto* options = static_cast<const retro_core_options_v2*>(data))
            self.defineOptions(options->definitions);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (const auto* intl = static_cast<const retro_core_options_v2_intl*>(data); intl && intl->us) {
            self.defineOptions(intl->us->definitions);
            if (intl->local)
                self.translateOptions(intl->local->definitions);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
        // Le core sait relier plusieurs consoles (câble link) : il nous donne
        // ses fonctions, qu'on appellera pendant une partie en mode câble.
        self.m_netpacket = data ? *static_cast<const retro_netpacket_callback*>(data) : retro_netpacket_callback{};
        self.m_netpacketVersion = data && self.m_netpacket.protocol_version ? self.m_netpacket.protocol_version : "";
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        // Notre langue : les cores traduits donnent leurs options en
        // français (et certains règlent la langue de la console émulée).
        *static_cast<unsigned*>(data) = RETRO_LANGUAGE_FRENCH;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        return true; // (notre écran montre toutes les options, sans en cacher)
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = static_cast<retro_variable*>(data);
        const auto it = var && var->key ? self.m_options.find(var->key) : self.m_options.end();
        if (it == self.m_options.end()) {
            if (var)
                var->value = nullptr;
            return false;
        }
        var->value = it->second.c_str(); // la chaîne vit dans m_options : le pointeur reste valable
        return true;
    }
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
        // Le core veut savoir si le son prend du retard (pour sauter le
        // dessin d'une image et rattraper, au lieu de faire grésiller le son).
        const auto* cb = static_cast<const retro_audio_buffer_status_callback*>(data);
        self.m_audioStatus = cb ? cb->callback : nullptr;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY: {
        // Le core a besoin d'au moins ce tampon (en ms) pour un son fluide.
        const unsigned ms = data ? *static_cast<const unsigned*>(data) : 0;
        self.m_audio.setMinimumLatency(ms / 1000.0);
        if (ms > 0)
            SDL_Log("Son : le core demande un tampon d'au moins %u ms", ms);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true; // on sait répondre à RETRO_DEVICE_ID_JOYPAD_MASK
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false; // pas encore de menu d'options
        return true;
    default:
        // Une demande que nous ne connaissons pas : répondre false est
        // toujours permis. EMU_CORE_LOG=1 les affiche (pour trouver ce qui
        // manque quand un core ne marche pas).
        if (SDL_getenv("EMU_CORE_LOG")) {
            static std::set<unsigned> seen;
            if (seen.insert(cmd).second)
                SDL_Log("Demande du core non gérée : %u%s", cmd & 0xFFFF,
                        (cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (expérimentale)" : "");
        }
        return false;
    }
}

void Core::defineOption(CoreOption option)
{
    const auto over = m_overrides.find(option.key);
    if (over != m_overrides.end()) {
        SDL_Log("Option %s = %s (core-options.txt)", option.key.c_str(), over->second.c_str());
        m_options[option.key] = over->second;
    } else {
        m_options[option.key] = option.defaultValue;
    }
    // Un core peut redéfinir ses options (ex. après le chargement du jeu).
    const auto same = std::find_if(m_optionList.begin(), m_optionList.end(),
                                   [&](const CoreOption& o) { return o.key == option.key; });
    if (same != m_optionList.end())
        *same = std::move(option);
    else
        m_optionList.push_back(std::move(option));
}

std::string Core::optionsSignature() const
{
    std::string changed;
    for (const CoreOption& o : m_optionList) {
        const auto it = m_options.find(o.key);
        if (it != m_options.end() && it->second != o.defaultValue)
            changed += o.key + '=' + it->second + ';';
    }
    if (changed.empty())
        return {};
    char hash[16];
    std::snprintf(hash, sizeof(hash), "%08x", static_cast<unsigned>(Hasher64::of(changed)));
    return std::string("options ") + hash;
}

void Core::onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    // data == NULL : image identique à la précédente (voir GET_CAN_DUPE).
    if (SDL_getenv("EMU_CORE_LOG")) {
        // Diagnostic : les premières images reçues, et de quel type.
        static int logged = 0;
        static int calls = 0, fresh = 0;
        ++calls;
        fresh += data != nullptr;
        if (logged < 5 && ++logged)
            SDL_Log("Image du core : %s %ux%u", data == RETRO_HW_FRAME_BUFFER_VALID ? "matérielle" : data ? "pixels" : "répétée",
                    width, height);
        if (calls % 300 == 0)
            SDL_Log("Images du core : %d nouvelles sur %d", fresh, calls);
    }
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
        g_current->m_video.hwFrameReady(width, height); // déjà dessinée dans notre framebuffer
    else if (data)
        g_current->m_video.upload(data, width, height, pitch);
}

void Core::onAudioSample(int16_t left, int16_t right)
{
    const int16_t frame[2] = {left, right};
    g_current->m_audio.push(frame, 1);
}

size_t Core::onAudioSampleBatch(const int16_t* data, size_t frames)
{
    g_current->m_audio.push(data, frames);
    return frames;
}

// --- Codes de triche ------------------------------------------------------------

int Core::applyCheats(const std::vector<std::pair<std::string, bool>>& codes)
{
    m_api.cheatReset();
    unsigned index = 0;
    for (const auto& [code, enabled] : codes) {
        if (!enabled)
            continue;
        // "7E0DBF63+7E0DC001" : plusieurs codes pour une même triche. Les
        // cores acceptent en général la chaîne entière, mais pas tous :
        // on les donne un par un, chacun sous son propre numéro.
        std::istringstream parts(code);
        std::string part;
        while (std::getline(parts, part, '+')) {
            part.erase(0, part.find_first_not_of(" \t"));
            part.erase(part.find_last_not_of(" \t") + 1);
            if (!part.empty())
                m_api.cheatSet(index++, true, part.c_str());
        }
    }
    if (index > 0)
        SDL_Log("%u code(s) de triche donné(s) au core", index);
    return static_cast<int>(index);
}

// --- Câble link (netpacket) -------------------------------------------------

std::string Core::netpacketVersion() const
{
    // Sans version de protocole, c'est la version du core qui compte.
    return m_netpacketVersion.empty() ? coreInfo() : m_netpacketVersion;
}

void Core::netpacketStart(uint16_t myId)
{
    if (!hasNetpacket() || m_netpacketStarted)
        return;
    m_netpacketStarted = true;
    // Le core garde ces deux fonctions pour envoyer et relever ses paquets.
    m_netpacket.start(myId, &Core::onNetpacketSend, &Core::onNetpacketPollReceive);
}

void Core::netpacketStop()
{
    if (!m_netpacketStarted)
        return;
    m_netpacketStarted = false;
    m_netpacketPump = nullptr;
    if (m_netpacket.stop)
        m_netpacket.stop();
}

bool Core::netpacketConnected(uint16_t id)
{
    return !m_netpacketStarted || !m_netpacket.connected || m_netpacket.connected(id);
}

void Core::netpacketDisconnected(uint16_t id)
{
    if (m_netpacketStarted && m_netpacket.disconnected)
        m_netpacket.disconnected(id);
}

void Core::netpacketReceive(const std::string& data, uint16_t from)
{
    if (m_netpacketStarted)
        m_netpacket.receive(data.data(), data.size(), from);
}

void Core::netpacketPoll()
{
    if (m_netpacketStarted && m_netpacket.poll)
        m_netpacket.poll();
}

std::vector<Core::NetPacket> Core::takeNetpackets()
{
    std::vector<NetPacket> out;
    out.swap(m_netOut);
    return out;
}

void RETRO_CALLCONV Core::onNetpacketSend(int, const void* buf, size_t len, uint16_t clientId)
{
    // On met de côté : App les envoie à la fin de l'image (le réseau n'est
    // pas utilisable depuis l'intérieur de retro_run). Tout est envoyé de
    // façon fiable, ce que la norme permet.
    if (g_current && buf && len > 0)
        g_current->m_netOut.push_back({clientId, std::string(static_cast<const char*>(buf), len)});
}

void RETRO_CALLCONV Core::onNetpacketPollReceive()
{
    // "Y a-t-il des paquets ?" Le core attend une réponse tout de suite
    // (melonDS DS : 25 ms au plus) : on fait le tour du réseau maintenant.
    Core* self = g_current;
    if (!self || !self->m_netpacketPump || self->m_pumping)
        return;
    self->m_pumping = true;
    self->m_netpacketPump();
    self->m_pumping = false;
}

void Core::onInputPoll()
{
    g_current->m_input.poll();
}

int16_t Core::onInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    return g_current->m_input.state(port, device, index, id);
}
