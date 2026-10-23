#pragma once
// Core : charge un "core" libretro (la DLL d'un émulateur) et dialogue avec lui.
//
// Un core ne sait rien faire seul : il ne dessine pas, ne joue pas de son et ne
// lit pas la manette. Il appelle des "callbacks" que nous lui fournissons
// (vidéo, audio, entrées, environnement). Nous sommes le "frontend".
//
// [C++] Une "class" est une struct C qui contient aussi des fonctions
// (les "méthodes") et qui distingue ce qui est public de ce qui est privé.
// Le constructeur est appelé automatiquement à la création de l'objet et le
// destructeur (~Core) à sa destruction. C'est le principe du RAII : la
// ressource (ici la DLL) est libérée sans qu'on ait à y penser, même en cas
// de "return" anticipé.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <libretro.h>

struct SDL_SharedObject;

// Une option d'un core, telle qu'il la décrit (pour l'écran des options).
struct CoreOption {
    std::string key;           // "mgba_solar_sensor_level"
    std::string label;         // "Niveau du capteur solaire"
    std::string info;          // explication (peut être vide)
    std::vector<std::pair<std::string, std::string>> values; // (valeur, libellé)
    std::string defaultValue;
};

class Video;
class Audio;
class Input;

class Core {
public:
    // [C++] Une référence (Video&) ressemble à un pointeur qui ne peut être ni
    // nul ni réaffecté. Pas besoin de "->" : on écrit video.present().
    Core(Video& video, Audio& audio, Input& input);
    ~Core();

    // [C++] On interdit la copie : deux Core qui posséderaient la même DLL la
    // libéreraient deux fois. "= delete" supprime ces opérations.
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    // Dossier où le core cherche ses BIOS (ex. scph5501.bin pour la PS1).
    void setSystemDirectory(std::string dir) { m_systemDir = std::move(dir); }
    // Dossier où le core peut écrire ses propres fichiers (cartes mémoire...).
    void setSaveDirectory(std::string dir) { m_saveDir = std::move(dir); }

    bool load(const std::filesystem::path& dllPath);
    // Chemin vide = lancer le core sans jeu (si le core le permet).
    bool loadGame(const std::filesystem::path& romPath);
    // Pourquoi le dernier loadGame a échoué, en clair (vide si inconnu).
    const std::string& loadError() const { return m_loadError; }
    void unloadGame();

    void runFrame();

    // Les types de manette que le core sait émuler (SET_CONTROLLER_INFO).
    struct ControllerType {
        std::string name; // "WiiMote + Nunchuk"
        unsigned id;      // à passer à retro_set_controller_port_device
    };
    const std::vector<std::vector<ControllerType>>& controllerTypes() const { return m_controllerTypes; }
    // Ce que fait chaque bouton dans la console émulée (SET_INPUT_DESCRIPTORS).
    struct InputDescriptor {
        unsigned port, device, index, id;
        std::string description; // "Wiimote 1", "Shake Wiimote"...
    };
    const std::vector<InputDescriptor>& inputDescriptors() const { return m_inputDescriptors; }
    // Branche ce type de manette sur ce port (0 = joueur 1).
    void setControllerType(unsigned port, unsigned id) { m_api.setControllerPortDevice(port, id); }
    void reset() { m_api.reset(); }

    // Save states : une photo complète de la mémoire de la console.
    // C'est aussi la brique de base du netplay "rollback".
    bool saveState(const std::filesystem::path& file) const;
    bool loadState(const std::filesystem::path& file);
    // La même chose, en mémoire (pour le jeu en ligne).
    bool serialize(std::string& out) const;
    bool unserialize(const std::string& data);
    bool canSerialize() const { return m_api.serializeSize && m_api.serializeSize() > 0; }
    size_t stateSize() const { return m_api.serializeSize ? m_api.serializeSize() : 0; }
    // Empreinte de la mémoire du JEU (RAM de la console + sauvegarde interne),
    // 0 si le core ne la montre pas. Contrairement à la sauvegarde d'état,
    // elle ignore les détails propres à chaque PC (tampons de son...).
    uint64_t memoryFingerprint() const;
    std::string coreInfo() const; // "Snes9x 1.62.3" : nom et version exacts
    // Ses sauvegardes d'état sont-elles assez fiables pour le rollback ?
    // (Le core peut le signaler lui-même ; sinon, liste des cas connus.)
    bool rollbackSafe() const;
    // Partie en ligne : les sauvegardes d'état servent au rollback. Le core
    // peut alors les adapter (FBNeo les rend identiques d'un PC à l'autre).
    void setNetplay(bool on) { m_netplay = on; }
    // Rendu préféré quand le core sait faire OpenGL ET Vulkan.
    void setPreferVulkan(bool on) { m_preferVulkan = on; }
    // Recalcul d'images (rollback) : le core peut sauter l'image et le son,
    // qui ne seront pas affichés. Bien plus rapide.
    void setSkipAudioVideo(bool skip) { m_skipAudioVideo = skip; }

    // Options du core ("mame_softlists_enable" = "enabled"...). Chaque core
    // annonce ses options et leur valeur par défaut, qu'on lui rend quand il
    // les demande. Ces valeurs-ci (lues dans core-options.txt) passent devant.
    // À appeler avant load().
    void setOptionOverrides(std::map<std::string, std::string> values) { m_overrides = std::move(values); }
    // Toutes les options annoncées par le core, dans son ordre.
    const std::vector<CoreOption>& optionList() const { return m_optionList; }
    // Résumé des options changées par l'utilisateur ("" si aucune). Deux
    // joueurs en ligne doivent avoir les mêmes, sinon leurs parties divergent.
    std::string optionsSignature() const;

    // --- Câble link (interface libretro "netpacket") ------------------------
    // Pour les jeux à plusieurs consoles reliées (câble link GBA, adaptateur
    // sans fil...) : chaque joueur fait tourner SA console, et le core
    // échange lui-même des paquets avec les autres. Nous ne faisons que les
    // transporter. Les numéros de joueurs : 0 = l'hôte, puis 1, 2, 3.
    struct NetPacket {
        uint16_t to; // RETRO_NETPACKET_BROADCAST = à tous
        std::string data;
    };
    bool hasNetpacket() const { return m_netpacket.start != nullptr; }
    // Version du protocole du core : deux joueurs doivent avoir la même.
    std::string netpacketVersion() const;
    void netpacketStart(uint16_t myId);
    void netpacketStop();
    bool netpacketConnected(uint16_t id);      // hôte : un joueur arrive
    void netpacketDisconnected(uint16_t id);   // hôte : un joueur part
    void netpacketReceive(const std::string& data, uint16_t from);
    void netpacketPoll();                      // une fois par image
    std::vector<NetPacket> takeNetpackets();   // ce que le core veut envoyer
    // Certains cores (melonDS DS) attendent les paquets des autres PENDANT
    // l'image, en appelant "poll_receive" : cette fonction (fournie par App)
    // envoie alors ce qui attend, lit le réseau et livre ce qui est arrivé.
    void setNetpacketPump(std::function<void()> pump) { m_netpacketPump = std::move(pump); }

    // Codes de triche : on remet à zéro, puis on donne au core ceux qui sont
    // activés (codes séparés par "+" : chacun envoyé à part). Renvoie le
    // nombre de codes appliqués.
    int applyCheats(const std::vector<std::pair<std::string, bool>>& codes);

    // SRAM : la sauvegarde "pile" de la cartouche, celle du jeu lui-même.
    bool saveSram(const std::filesystem::path& file) const;
    bool loadSram(const std::filesystem::path& file);

    const retro_system_info& info() const { return m_info; }
    const retro_system_av_info& avInfo() const { return m_avInfo; }

private:
    // Les callbacks libretro sont des pointeurs de fonctions C : ils ne
    // peuvent pas pointer vers une méthode d'un objet. On passe donc par des
    // fonctions "static" (sans objet), qui retrouvent l'instance active.
    static bool onEnvironment(unsigned cmd, void* data);
    static void onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
    static void onAudioSample(int16_t left, int16_t right);
    static size_t onAudioSampleBatch(const int16_t* data, size_t frames);
    static void onInputPoll();
    static int16_t onInputState(unsigned port, unsigned device, unsigned index, unsigned id);
    static void RETRO_CALLCONV onNetpacketSend(int flags, const void* buf, size_t len, uint16_t clientId);
    static void RETRO_CALLCONV onNetpacketPollReceive();

    void applyAvInfo();
    void defineOption(CoreOption option);
    // [C++] Un "template" : une fonction écrite une fois pour plusieurs
    // types. Les définitions d'options existent en version 1 et 2 (deux
    // structs différentes, mais avec les mêmes champs key/desc/info/values).
    template <typename Definition>
    void defineOptions(const Definition* definitions)
    {
        for (; definitions && definitions->key; ++definitions) {
            CoreOption o;
            o.key = definitions->key;
            o.label = definitions->desc ? definitions->desc : definitions->key;
            o.info = definitions->info ? definitions->info : "";
            for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && definitions->values[i].value; ++i) {
                const retro_core_option_value& v = definitions->values[i];
                o.values.emplace_back(v.value, v.label ? v.label : v.value);
            }
            o.defaultValue = definitions->default_value ? definitions->default_value
                             : o.values.empty()          ? std::string()
                                                         : o.values.front().first;
            defineOption(std::move(o));
        }
    }
    // Les traductions fournies par le core (dans la langue qu'on lui a
    // annoncée : le français) remplacent les textes anglais.
    template <typename Definition>
    void translateOptions(const Definition* local)
    {
        for (; local && local->key; ++local) {
            for (CoreOption& o : m_optionList) {
                if (o.key != local->key)
                    continue;
                if (local->desc)
                    o.label = local->desc;
                if (local->info)
                    o.info = local->info;
                for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && local->values[i].value; ++i)
                    for (auto& [value, label] : o.values)
                        if (value == local->values[i].value && local->values[i].label)
                            label = local->values[i].label;
            }
        }
    }

    // Les fonctions exportées par la DLL. [C++] decltype(&retro_init) donne
    // le type exact du pointeur de fonction : pas besoin de recopier les
    // signatures à la main comme on le ferait avec des typedef en C.
    struct Api {
        decltype(&retro_init) init = nullptr;
        decltype(&retro_deinit) deinit = nullptr;
        decltype(&retro_api_version) apiVersion = nullptr;
        decltype(&retro_get_system_info) getSystemInfo = nullptr;
        decltype(&retro_get_system_av_info) getSystemAvInfo = nullptr;
        decltype(&retro_set_environment) setEnvironment = nullptr;
        decltype(&retro_set_video_refresh) setVideoRefresh = nullptr;
        decltype(&retro_set_audio_sample) setAudioSample = nullptr;
        decltype(&retro_set_audio_sample_batch) setAudioSampleBatch = nullptr;
        decltype(&retro_set_input_poll) setInputPoll = nullptr;
        decltype(&retro_set_input_state) setInputState = nullptr;
        decltype(&retro_set_controller_port_device) setControllerPortDevice = nullptr;
        decltype(&retro_reset) reset = nullptr;
        decltype(&retro_run) run = nullptr;
        decltype(&retro_serialize_size) serializeSize = nullptr;
        decltype(&retro_serialize) serialize = nullptr;
        decltype(&retro_unserialize) unserialize = nullptr;
        decltype(&retro_load_game) loadGame = nullptr;
        decltype(&retro_unload_game) unloadGame = nullptr;
        decltype(&retro_get_memory_data) getMemoryData = nullptr;
        decltype(&retro_get_memory_size) getMemorySize = nullptr;
        decltype(&retro_cheat_reset) cheatReset = nullptr;
        decltype(&retro_cheat_set) cheatSet = nullptr;
    };

    Video& m_video;
    Audio& m_audio;
    Input& m_input;

    SDL_SharedObject* m_handle = nullptr;
    Api m_api;
    bool m_initialized = false;
    bool m_gameLoaded = false;
    bool m_supportsNoGame = false;
    std::vector<std::vector<ControllerType>> m_controllerTypes; // un vecteur par port
    std::vector<InputDescriptor> m_inputDescriptors;
    uint64_t m_serializationQuirks = 0;
    bool m_netplay = false;
    bool m_preferVulkan = false;
    std::map<std::string, std::string> m_options;   // option -> valeur actuelle
    std::map<std::string, std::string> m_overrides; // choisies par l'utilisateur
    std::vector<CoreOption> m_optionList;
    retro_netpacket_callback m_netpacket{}; // fourni par le core (tout à nullptr sinon)
    std::string m_netpacketVersion;         // copie : le pointeur du core peut changer
    bool m_netpacketStarted = false;
    std::vector<NetPacket> m_netOut;
    std::function<void()> m_netpacketPump;
    bool m_pumping = false; // (pas de pompe dans la pompe)
    bool m_skipAudioVideo = false;

    retro_system_info m_info{};
    retro_system_av_info m_avInfo{};

    // [C++] std::string gère sa mémoire tout seul (plus de malloc/free).
    // Le core garde les pointeurs c_str() qu'on lui donne : ces chaînes
    // doivent donc vivre aussi longtemps que lui, d'où ces membres.
    std::string m_systemDir;
    std::string m_saveDir;
    std::string m_romPath;
    std::string m_loadError;
    // [C++] std::vector = un tableau dynamique qui se libère tout seul.
    std::vector<uint8_t> m_romData;
};
