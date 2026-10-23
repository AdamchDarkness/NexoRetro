#include "Library.hpp"

#include "UserData.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Les cores proposés par défaut sont tous disponibles sur le buildbot
// libretro. Les jeux sur CD (PS1...) sont repérés par leur .cue/.chd et pas
// par leurs .bin, sinon chaque piste apparaîtrait comme un jeu.
constexpr System kSystems[] = {
    {"nes", "Nintendo NES", "NES", 0xB91C1C, "mesen_libretro.dll", "nes fds unf unif", "nes famicom"},
    {"snes", "Super Nintendo", "SNES", 0x4C3FB8, "snes9x_libretro.dll", "sfc smc", "snes super nintendo"},
    {"n64", "Nintendo 64", "N64", 0x1F7A47, "mupen64plus_next_libretro.dll", "n64 z64 v64", "n64 nintendo 64"},
    // Game Boy et Game Boy Color : le même émulateur (Gambatte), deux consoles.
    {"gb", "Game Boy", "GB", 0x3F4A5A, "gambatte_libretro.dll", "gb", "gameboy game boy"},
    {"gbc", "Game Boy Color", "GBC", 0x7C3AED, "gambatte_libretro.dll", "gbc", "gbc color"},
    {"gba", "Game Boy Advance", "GBA", 0x0F766E, "mgba_libretro.dll", "gba", "gba advance"},
    {"nds", "Nintendo DS", "DS", 0x475569, "melondsds_libretro.dll", "nds", "nds nintendo ds"},
    // GameCube et Wii : le même émulateur (Dolphin), mais deux consoles. Les
    // fichiers ont les mêmes extensions : c'est le disque lui-même qui dit
    // laquelle (voir dolphinDiscKind).
    {"gc", "GameCube", "GC", 0x5B3FA0, "dolphin_libretro.dll", "iso gcm rvz ciso gcz", "gamecube gc ngc"},
    {"wii", "Wii", "WII", 0x0EA5E9, "dolphin_libretro.dll", "iso rvz wbfs ciso gcz wia wad", "wii"},
    {"md", "Mega Drive / Genesis", "MD", 0x1E3A8A, "genesis_plus_gx_libretro.dll", "md gen smd", "megadrive mega drive genesis"},
    // Le lecteur CD de la Mega Drive : même émulateur, mais un BIOS obligatoire.
    {"segacd", "Sega CD / Mega-CD", "SCD", 0x0F172A, "genesis_plus_gx_libretro.dll", "cue chd iso m3u",
     "segacd sega cd megacd mega-cd mega cd"},
    // Saturn : Beetle Saturn (fidèle, BIOS obligatoire), Yabause en secours.
    {"saturn", "Sega Saturn", "SAT", 0x334155, "mednafen_saturn_libretro.dll", "cue chd ccd m3u", "saturn",
     "yabause_libretro.dll"},
    // Atari : une console par génération, chacune son émulateur.
    {"a2600", "Atari 2600", "2600", 0x92400E, "stella_libretro.dll", "a26", "2600 vcs"},
    {"a5200", "Atari 5200", "5200", 0x78350F, "a5200_libretro.dll", "a52", "5200"},
    {"a7800", "Atari 7800", "7800", 0x9A3412, "prosystem_libretro.dll", "a78", "7800"},
    {"lynx", "Atari Lynx", "LYNX", 0xB45309, "handy_libretro.dll", "lnx", "lynx"},
    {"jaguar", "Atari Jaguar", "JAG", 0x7F1D1D, "virtualjaguar_libretro.dll", "j64 jag", "jaguar"},
    {"ps1", "PlayStation", "PS1", 0x6B6E78, "pcsx_rearmed_libretro.dll", "cue chd pbp m3u", "ps1 psx playstation 1"},
    {"ps2", "PlayStation 2", "PS2", 0x1E2A78, "pcsx2_libretro.dll", "iso chd cso", "ps2 playstation 2"},
    {"psp", "PSP", "PSP", 0x334155, "ppsspp_libretro.dll", "iso cso pbp", "psp"},
    {"dc", "Dreamcast", "DC", 0xE85D04, "flycast_libretro.dll", "gdi cdi chd cue m3u", "dreamcast"},
    {"3ds", "Nintendo 3DS", "3DS", 0xB91C5C, "citra_libretro.dll", "3ds 3dsx cci cxi", "3ds"},
    // Consoles récentes : pas de core libretro, un émulateur externe lance
    // le jeu (voir System::external). La 360 AVANT la Xbox : un dossier
    // "Xbox 360" contient aussi le mot "xbox".
    {"switch", "Nintendo Switch", "SWITCH", 0xE60012, "", "nsp xci nsz xcz", "switch", nullptr, "Ryujinx", "Ryujinx.exe",
     "{rom}"},
    {"x360", "Xbox 360", "360", 0x5DC21E, "", "iso xex zar", "360 x360", nullptr, "Xenia", "xenia_canary.exe", "{rom}"},
    {"xbox", "Xbox", "XBOX", 0x107C10, "", "iso xiso", "xbox", nullptr, "xemu", "xemu.exe", "-dvd_path {rom}"},
    {"ps3", "PlayStation 3", "PS3", 0x2B2B2B, "", "iso bin", "ps3 rpcs3", nullptr, "RPCS3", "rpcs3.exe",
     "--no-gui {rom}"},
    {"ps4", "PlayStation 4", "PS4", 0x003791, "", "bin", "ps4 shadps4", nullptr, "shadPS4", "shadPS4.exe", "{rom}"},
    // Arcade et Neo Geo : un jeu = un .zip (un "romset"), dont le nom est
    // imposé par l'émulateur ("kof98.zip"). FBNeo est le core de référence
    // pour jouer en ligne (c'est celui de Fightcade) ; MAME en secours.
    {"arcade", "Arcade", "ARC", 0xC2410C, "fbneo_libretro.dll", "zip 7z", "arcade mame fbneo fba cps capcom",
     "mame2003_plus_libretro.dll mame_libretro.dll"},
    {"neogeo", "Neo Geo", "NEO", 0xA16207, "fbneo_libretro.dll", "zip 7z", "neogeo neo-geo",
     "mame2003_plus_libretro.dll mame_libretro.dll"},
};

// Les .zip d'arcade qui ne sont pas des jeux : des BIOS et des puces
// partagées par plusieurs jeux. Ils restent à côté des jeux (l'émulateur
// les y cherche) mais n'apparaissent pas dans la bibliothèque.
constexpr const char* kArcadeSupportFiles[] = {
    "neogeo", "pgm", "qsound", "qsound_hle", "decocass", "skns", "nmk004", "isgsm", "cchip", "ym2608", "bubsys",
    "stvbios", "naomi", "awbios", "hod2bios", "cpzn1", "cpzn2", "tps", "konamigx", "megaplay", "megatech",
    "nss", "playch10", "ssv", "midssio", "sega315-5881", "namcoc69", "namcoc70", "namcoc75", "coh1000c", "coh3002c",
};

bool isArcade(const System& system)
{
    return std::string_view(system.id) == "arcade" || std::string_view(system.id) == "neogeo";
}

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool hasWord(const char* list, const std::string& word)
{
    // [C++] std::istringstream découpe une chaîne mot par mot, comme strtok
    // mais sans modifier la chaîne d'origine.
    std::istringstream words(list);
    std::string w;
    while (words >> w) {
        if (w == word)
            return true;
    }
    return false;
}

std::string extensionOf(const fs::path& file)
{
    const std::string ext = toLower(toUtf8(file.extension()));
    return ext.size() > 1 ? ext.substr(1) : std::string();
}

// GameCube ou Wii ? Le disque le dit lui-même, quel que soit son dossier :
//  - .rvz / .wia (format compressé de Dolphin) : à l'adresse 0x48, le type
//    de disque (1 = GameCube, 2 = Wii) ;
//  - .ciso : un en-tête de 0x8000 octets, puis le disque ;
//  - disque brut (.iso, .gcm) : une "signature magique" dans l'en-tête,
//    0x5D1C9EA3 à l'adresse 0x18 pour la Wii, 0xC2339F3D à 0x1C pour la GameCube.
// Renvoie "gc", "wii", ou "" si on ne sait pas (fichier .gcz compressé...).
std::string_view dolphinDiscKind(const fs::path& file)
{
    const std::string ext = extensionOf(file);
    if (ext == "wbfs" || ext == "wad")
        return "wii"; // formats propres à la Wii
    std::ifstream in(file, std::ios::binary);
    unsigned char head[0x60] = {};
    if (!in.read(reinterpret_cast<char*>(head), sizeof(head)))
        return {};
    // [C++] Les nombres des consoles Nintendo sont "big-endian" (octet de
    // poids fort en premier), à l'inverse du PC : on les assemble à la main.
    auto be32 = [](const unsigned char* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    };
    if (std::memcmp(head, "RVZ\x01", 4) == 0 || std::memcmp(head, "WIA\x01", 4) == 0) {
        const uint32_t type = be32(head + 0x48);
        return type == 2 ? "wii" : type == 1 ? "gc" : std::string_view();
    }
    if (std::memcmp(head, "CISO", 4) == 0) {
        in.seekg(0x8000);
        if (!in.read(reinterpret_cast<char*>(head), sizeof(head)))
            return {};
    }
    if (be32(head + 0x18) == 0x5D1C9EA3)
        return "wii";
    if (be32(head + 0x1C) == 0xC2339F3D)
        return "gc";
    return {};
}

} // namespace

std::string cleanTitle(const std::string& fileStem)
{
    // On retire tout ce qui est entre () ou [] : région, version, langues...
    std::string result;
    int depth = 0;
    for (char c : fileStem) {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        else if (depth == 0)
            result += (c == '_') ? ' ' : c;
    }
    // Espaces en trop au début, à la fin, et en double au milieu.
    std::string compact;
    for (char c : result) {
        if (c == ' ' && (compact.empty() || compact.back() == ' '))
            continue;
        compact += c;
    }
    while (!compact.empty() && compact.back() == ' ')
        compact.pop_back();
    return compact.empty() ? fileStem : compact;
}

std::string regionOf(const std::string& fileStem)
{
    struct Tag {
        const char* key;
        const char* label;
    };
    static const Tag tags[] = {{"(europe", "Europe"}, {"(france", "France"}, {"(usa", "USA"}, {"(japan", "Japon"},
                               {"(world", "Monde"},   {"(e)", "Europe"},    {"(u)", "USA"},   {"(j)", "Japon"},
                               {"(f)", "France"}};
    const std::string lower = toLower(fileStem);
    for (const Tag& tag : tags) {
        if (lower.find(tag.key) != std::string::npos)
            return tag.label;
    }
    return {};
}

Library::Library(fs::path romsDir, fs::path coresDir)
    : m_romsDir(std::move(romsDir)), m_coresDir(std::move(coresDir))
{
}

std::span<const System> Library::systems()
{
    return kSystems;
}

const System* Library::systemById(std::string_view id)
{
    for (const System& s : kSystems) {
        if (id == s.id)
            return &s;
    }
    return nullptr;
}

std::vector<const System*> Library::candidatesFor(const fs::path& file)
{
    std::vector<const System*> result;
    const std::string ext = extensionOf(file);
    if (ext.empty())
        return result;
    for (const System& s : kSystems) {
        if (hasWord(s.extensions, ext))
            result.push_back(&s);
    }
    return result;
}

const System* Library::guessSystem(const fs::path& file)
{
    const std::vector<const System*> candidates = candidatesFor(file);
    if (candidates.size() == 1)
        return candidates.front();
    if (candidates.empty())
        return nullptr;

    // GameCube ou Wii parmi les possibles : le disque le dit lui-même.
    const bool dolphin = std::any_of(candidates.begin(), candidates.end(), [](const System* s) {
        return std::string_view(s->id) == "gc" || std::string_view(s->id) == "wii";
    });
    if (dolphin)
        if (const std::string_view kind = dolphinDiscKind(file); !kind.empty())
            return systemById(kind);

    // Plusieurs consoles possibles (un .iso par exemple) : on cherche un
    // indice dans les noms de dossiers ("D:\Jeux\PS2\...").
    for (fs::path dir = file.parent_path(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path()) {
        const std::string name = toLower(toUtf8(dir.filename()));
        for (const System* s : candidates) {
            std::istringstream words(s->hints);
            std::string hint;
            while (words >> hint) {
                if (name == hint || name.find(hint) != std::string::npos)
                    return s;
            }
        }
    }
    return nullptr;
}

void Library::addGame(const fs::path& path, const System& system)
{
    if (isArcade(system)) {
        const std::string stem = toLower(toUtf8(path.stem()));
        for (const char* support : kArcadeSupportFiles) {
            if (stem == support) {
                m_biosFound.push_back(path);
                return;
            }
        }
    }
    std::error_code ec;
    Game game;
    std::string stem = toUtf8(path.stem());
    // PS3 / PS4 : un jeu installé est un dossier dont le programme s'appelle
    // EBOOT.BIN (JEU/PS3_GAME/USRDIR/EBOOT.BIN). Les autres .bin ne sont pas
    // des jeux ; le nom du jeu est celui de son dossier.
    const std::string_view id = system.id;
    if ((id == "ps3" || id == "ps4") && extensionOf(path) == "bin") {
        if (toLower(stem) != "eboot")
            return;
        for (fs::path dir = path.parent_path(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path()) {
            const std::string name = toUtf8(dir.filename());
            const std::string low = toLower(name);
            if (low != "usrdir" && low != "ps3_game" && !name.empty()) {
                stem = name;
                break;
            }
        }
    }
    game.title = cleanTitle(stem);
    game.region = regionOf(stem);
    game.path = path;
    game.system = &system;
    game.size = fs::file_size(path, ec);
    m_games.push_back(std::move(game));
}

void Library::scan(const UserData& userData)
{
    m_games.clear();
    m_biosFound.clear();
    std::error_code ec;
    fs::create_directories(m_coresDir, ec);
    // Un même fichier peut être trouvé deux fois (roms/ + dossier surveillé).
    std::set<std::string> seen;
    auto add = [&](const fs::path& path, const System& chosen) {
        if (!seen.insert(UserData::keyOf(path)).second || userData.isHidden(path))
            return;
        // Un jeu rangé (ou ajouté) en GameCube qui est en fait un disque Wii,
        // ou l'inverse : on se fie au disque (les jeux ajoutés avant la
        // séparation des deux consoles étaient tous "GameCube / Wii").
        const System* system = &chosen;
        if (std::string_view(chosen.id) == "gc" || std::string_view(chosen.id) == "wii")
            if (const std::string_view kind = dolphinDiscKind(path); !kind.empty())
                system = systemById(kind);
        addGame(path, *system);
        // Nom choisi par l'utilisateur, à la place de celui du fichier.
        if (const GameRecord* r = userData.find(path); r && !r->title.empty())
            m_games.back().title = r->title;
    };
    auto walk = [&](const fs::path& dir, auto&& onFile) {
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec)
                break;
            if (it->is_regular_file(ec))
                onFile(it->path());
        }
    };

    // 1. roms/<console>/ : la console est donnée par le dossier.
    for (const System& system : kSystems) {
        const fs::path dir = m_romsDir / system.id;
        fs::create_directories(dir, ec);
        walk(dir, [&](const fs::path& file) {
            if (hasWord(system.extensions, extensionOf(file)))
                add(file, system);
        });
    }
    // 2. Les fichiers ajoutés un par un, avec la console choisie.
    for (const auto& [key, systemId] : userData.manualGames()) {
        const fs::path file = fromUtf8(key);
        if (const System* system = systemById(systemId); system && fs::exists(file, ec))
            add(file, *system);
    }
    // 3. Les dossiers surveillés : la console est devinée.
    for (const fs::path& folder : userData.watchedFolders()) {
        walk(folder, [&](const fs::path& file) {
            if (const System* system = guessSystem(file))
                add(file, *system);
        });
    }

    // Tri alphabétique sans tenir compte des majuscules.
    // [C++] Le 3e argument est une lambda qui dit si a doit passer avant b.
    std::sort(m_games.begin(), m_games.end(),
              [](const Game& a, const Game& b) { return toLower(a.title) < toLower(b.title); });

    SDL_Log("Bibliothèque : %zu jeux trouvés", m_games.size());
}

const Game* Library::find(const fs::path& path) const
{
    const std::string key = UserData::keyOf(path);
    for (const Game& g : m_games) {
        if (UserData::keyOf(g.path) == key)
            return &g;
    }
    return nullptr;
}

int Library::countFor(const System& system) const
{
    // [C++] std::count_if compte les éléments pour lesquels la lambda renvoie true.
    return static_cast<int>(
        std::count_if(m_games.begin(), m_games.end(), [&](const Game& g) { return g.system == &system; }));
}

std::string Library::coreFileFor(const Game& game) const
{
    // Les cores imposés par le jeu (les autres ne le connaissent pas), sinon
    // ceux de la console, dans l'ordre de préférence.
    std::string list;
    if (game.preferredCores) {
        list = game.preferredCores;
    } else {
        list = game.system->coreFile;
        if (game.system->otherCores)
            list += std::string(" ") + game.system->otherCores;
    }
    std::istringstream cores(list);
    std::string file, first;
    std::error_code ec;
    while (cores >> file) {
        if (first.empty())
            first = file;
        if (fs::exists(m_coresDir / file, ec))
            return file;
    }
    return first;
}

std::vector<fs::path> Library::filesOf(const fs::path& game)
{
    std::vector<fs::path> files;
    std::error_code ec;
    auto addFile = [&](const fs::path& f) {
        if (fs::is_regular_file(f, ec) && std::find(files.begin(), files.end(), f) == files.end())
            files.push_back(f);
    };
    // Lit les noms de fichiers cités dans un .cue ("FILE "piste 1.bin" BINARY")
    // ou un .m3u (un disque par ligne), relatifs au dossier de ce fichier.
    auto addListed = [&](const fs::path& list, auto&& self) -> void {
        addFile(list);
        const std::string ext = extensionOf(list);
        if (ext != "cue" && ext != "m3u")
            return;
        std::ifstream in(list);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::string name;
            if (ext == "cue") {
                const size_t start = line.find('"');
                const size_t end = start == std::string::npos ? start : line.find('"', start + 1);
                if (line.find("FILE") == std::string::npos || end == std::string::npos)
                    continue;
                name = line.substr(start + 1, end - start - 1);
            } else if (!line.empty() && line[0] != '#') {
                name = line;
            }
            if (!name.empty())
                self(list.parent_path() / fromUtf8(name), self);
        }
    };
    addListed(game, addListed);
    return files;
}

void Library::refine(const std::function<bool(Game&)>& change)
{
    bool changed = false;
    for (Game& g : m_games)
        changed |= change(g);
    if (changed)
        std::sort(m_games.begin(), m_games.end(),
                  [](const Game& a, const Game& b) { return toLower(a.title) < toLower(b.title); });
}

bool Library::hasCore(const Game& game) const
{
    std::error_code ec;
    if (game.system->external()) {
        const fs::path exe = externalPath(*game.system);
        return !exe.empty() && fs::is_regular_file(exe, ec);
    }
    return fs::is_regular_file(corePath(game), ec);
}

fs::path Library::externalPath(const System& system) const
{
    // Le chemin choisi dans Paramètres, sinon le programme rangé dans cores/.
    const auto it = m_externalPaths.find(system.id);
    if (it != m_externalPaths.end() && !it->second.empty())
        return it->second;
    std::error_code ec;
    const fs::path inCores = m_coresDir / system.externalExe;
    return fs::is_regular_file(inCores, ec) ? inCores : fs::path();
}
