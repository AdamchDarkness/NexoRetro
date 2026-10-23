#include "Bios.hpp"

#include "Hash.hpp"
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

// Un BIOS de PS2 fait toujours 4 Mo ; son nom varie selon le modèle de la
// console : on le range sous le nom que lui donne la base.
constexpr std::uintmax_t kPs2BiosSize = 4u * 1024u * 1024u;
// Au-delà, ce n'est pas un BIOS : inutile de lire le fichier (un dossier
// "Téléchargements" peut contenir des jeux de plusieurs Go).
constexpr std::uintmax_t kMaxBiosSize = 64u * 1024u * 1024u;
// Emplacements attendus par les cores (relatifs au dossier bios/).
const fs::path kPs2Dir = fs::path("pcsx2") / "bios";
const fs::path kDolphinSys = fs::path("dolphin-emu") / "Sys";
// FBNeo cherche ses BIOS à côté du jeu, ou dans bios/fbneo/.
const fs::path kNeoGeoBios = fs::path("fbneo") / "neogeo.zip";
// Flycast (Dreamcast) cherche ses BIOS dans bios/dc/.
const fs::path kDreamcastDir = fs::path("dc");

// Sans la base (fichier absent), on reconnaît encore les BIOS à leur nom.
struct Known {
    const char* file;   // nom attendu, en minuscules
    const char* system; // nom affiché
};
const Known kKnown[] = {
    {"scph5500.bin", "PlayStation"}, {"scph5501.bin", "PlayStation"}, {"scph5502.bin", "PlayStation"},
    {"gba_bios.bin", "Game Boy Advance"}, {"bios7.bin", "Nintendo DS"}, {"bios9.bin", "Nintendo DS"},
    {"firmware.bin", "Nintendo DS"},
};

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string baseName(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// "Sony - PlayStation 2" -> "PlayStation 2" (le fabricant est en trop à l'écran).
std::string shortSystem(const std::string& system)
{
    const size_t dash = system.find(" - ");
    return dash == std::string::npos ? system : system.substr(dash + 3);
}

// "ps2-0230e-20080220.bin" -> "PS2 v2.30 · Europe".
std::string ps2Version(const std::string& path)
{
    const std::string name = baseName(path);
    if (name.size() < 9 || name.compare(0, 4, "ps2-") != 0)
        return name;
    const std::string digits = name.substr(4, 4);
    const char region = name[8];
    std::string text = "PS2 v" + std::to_string(std::atoi(digits.substr(0, 2).c_str())) + "." + digits.substr(2, 2);
    switch (region) {
    case 'e': text += " · Europe"; break;
    case 'a': text += " · Amérique du Nord"; break;
    case 'j': text += " · Japon"; break;
    case 'c': text += " · Chine"; break;
    case 'h': text += " · Asie"; break;
    default: break;
    }
    return text;
}

// Chaque BIOS de PS2 porte sa "carte d'identité" en clair (l'entrée ROMVER) :
// "0160EC20011004" = version 1.60, Europe (E), console (C), 4 octobre 2001.
// Utile pour les copies absentes de la base (dumps différents de celui de
// référence) : PCSX2 les accepte, il ne regarde que cette carte.
// Renvoie un nom au format de la base ("ps2-0160e-20011004.bin"), ou "".
std::string ps2RomVer(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::string head(64 * 1024, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    auto digits = [&](size_t from, size_t count) {
        for (size_t k = from; k < from + count; ++k)
            if (!std::isdigit(static_cast<unsigned char>(head[k])))
                return false;
        return true;
    };
    for (size_t i = 0; i + 14 <= head.size(); ++i) {
        const char region = head[i + 4];
        const char kind = head[i + 5];
        if (digits(i, 4) && std::strchr("JAEHCT", region) && region && (kind == 'C' || kind == 'D') && digits(i + 6, 8))
            return "ps2-" + head.substr(i, 4) + static_cast<char>(std::tolower(region)) + "-" + head.substr(i + 6, 8) + ".bin";
    }
    return {};
}

// Le firmware de la DS contient les RÉGLAGES de la console (pseudo, date de
// naissance, langue, calibrage de l'écran tactile, adresse Wi-Fi) : chaque
// copie est unique, aucune base d'empreintes ne peut la reconnaître. On lit
// donc son en-tête : 256 Ko, "MAC" à l'adresse 8, et le modèle de console à
// l'adresse 0x1D. Renvoie le modèle ("DS Lite"...), ou "".
std::string dsFirmwareModel(const fs::path& file)
{
    std::error_code ec;
    if (fs::file_size(file, ec) != 256 * 1024)
        return {};
    std::ifstream in(file, std::ios::binary);
    char head[0x20] = {};
    in.read(head, sizeof(head));
    if (!in || std::memcmp(head + 8, "MAC", 3) != 0)
        return {};
    switch (static_cast<unsigned char>(head[0x1D])) {
    case 0xFF: return "DS";
    case 0x20: return "DS Lite";
    case 0x57: return "DSi";
    case 0x43: return "iQue DS";
    case 0x63: return "iQue DS Lite";
    default: return "DS";
    }
}

// Ce qu'on reconnaît à l'en-tête, pour les BIOS absents de la base : la
// console (pour le message) et l'endroit où le ranger. Vide sinon.
struct HeaderIdentity {
    std::string label;
    fs::path target; // relatif à bios/ (le nom d'origine est gardé si vide)
};
std::optional<HeaderIdentity> identifyByHeader(const fs::path& file)
{
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (size == 4u * 1024 * 1024) {
        if (const std::string version = ps2RomVer(file); !version.empty())
            return HeaderIdentity{ps2Version(version), fs::path("pcsx2") / "bios" / file.filename()};
    }
    if (const std::string model = dsFirmwareModel(file); !model.empty())
        return HeaderIdentity{"Nintendo DS (firmware " + model + ")", "firmware.bin"};
    return std::nullopt;
}

// Certains noms de la base sont anciens : on range aussi le fichier sous le
// nom qu'attend le core qu'on utilise.
std::vector<std::string> withAliases(std::vector<std::string> paths)
{
    for (size_t i = 0, n = paths.size(); i < n; ++i)
        if (paths[i] == "dc/flash.bin")
            paths.push_back("dc/dc_flash.bin"); // Flycast
    return paths;
}

} // namespace

BiosManager::BiosManager(fs::path biosDir, const fs::path& database) : m_dir(std::move(biosDir))
{
    loadDatabase(database);
    refresh();
}

// --- La base des BIOS ---------------------------------------------------------------

void BiosManager::loadDatabase(const fs::path& file)
{
    // Format "clrmamepro" : des blocs de texte, avec pour chaque BIOS une ligne
    //   rom ( name "dc/dc_boot.bin" size 2097152 crc 89F2B1A1 md5 ... )
    // précédée d'un commentaire qui donne la console :
    //   comment "Sega - Dreamcast"
    std::ifstream in(file);
    std::string line, system;
    while (std::getline(in, line)) {
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        const std::string_view text = std::string_view(line).substr(start);
        if (text.starts_with("comment \"")) {
            system = std::string(text.substr(9, text.find('"', 9) - 9));
            continue;
        }
        if (!text.starts_with("rom ("))
            continue;

        DbEntry e;
        e.system = system;
        size_t pos = text.find("name ");
        if (pos == std::string_view::npos)
            continue;
        pos += 5;
        if (text[pos] == '"') { // nom entre guillemets : il peut contenir des espaces
            const size_t end = text.find('"', pos + 1);
            e.path = std::string(text.substr(pos + 1, end - pos - 1));
        } else {
            e.path = std::string(text.substr(pos, text.find(' ', pos) - pos));
        }
        // [C++] std::stoull / std::stoul : texte -> nombre (base 16 pour le CRC).
        const size_t sizePos = text.find(" size ");
        const size_t crcPos = text.find(" crc ");
        if (sizePos == std::string_view::npos || crcPos == std::string_view::npos)
            continue;
        e.size = std::stoull(std::string(text.substr(sizePos + 6, 12)));
        e.crc = static_cast<uint32_t>(std::stoul(std::string(text.substr(crcPos + 5, 8)), nullptr, 16));
        m_bySize[e.size].push_back(m_db.size());
        m_db.push_back(std::move(e));
    }
    SDL_Log("Base des BIOS : %zu fichiers connus", m_db.size());
}

std::optional<uint32_t> BiosManager::crcOf(const fs::path& file) const
{
    // Un même fichier est relu à chaque rafraîchissement de l'écran BIOS :
    // on garde son CRC tant que sa taille et sa date ne changent pas.
    std::error_code ec;
    const uint64_t size = fs::file_size(file, ec);
    if (ec)
        return std::nullopt;
    const long long time = fs::last_write_time(file, ec).time_since_epoch().count();
    if (const auto it = m_crcCache.find(file); it != m_crcCache.end() && it->second.size == size && it->second.time == time)
        return it->second.crc;

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<char> buffer(1 << 16);
    uint32_t crc = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        crc = crc32(crc, buffer.data(), static_cast<size_t>(in.gcount()));
    }
    m_crcCache[file] = {size, time, crc};
    return crc;
}

std::optional<BiosManager::Identity> BiosManager::identify(const fs::path& file) const
{
    std::error_code ec;
    const uint64_t size = fs::file_size(file, ec);
    // D'abord la taille (gratuite) : la plupart des fichiers s'arrêtent là.
    const auto candidates = m_bySize.find(size);
    if (ec || candidates == m_bySize.end())
        return std::nullopt;
    const std::optional<uint32_t> crc = crcOf(file);
    if (!crc)
        return std::nullopt;
    std::optional<Identity> id;
    for (size_t index : candidates->second) {
        const DbEntry& e = m_db[index];
        if (e.crc != *crc)
            continue;
        if (!id)
            id = Identity{e.system, {}};
        if (std::find(id->paths.begin(), id->paths.end(), e.path) == id->paths.end())
            id->paths.push_back(e.path);
    }
    return id;
}

// --- État de chaque console ------------------------------------------------------------

BiosFile BiosManager::describe(const fs::path& rel, const std::string& label, const std::string& region) const
{
    BiosFile f;
    f.label = label;
    std::error_code ec;
    f.present = fs::is_regular_file(m_dir / rel, ec);
    if (!f.present) {
        f.note = region.empty() ? "Manquant" : region;
        return f;
    }
    const std::optional<Identity> id = m_db.empty() ? std::nullopt : identify(m_dir / rel);
    const std::string expected = toLower(toUtf8(rel.filename()));
    if (m_db.empty()) {
        f.check = BiosCheck::Verified; // pas de base : on se fie au nom
    } else if (!id) {
        f.check = BiosCheck::Unknown;
    } else {
        f.check = BiosCheck::Other;
        for (const std::string& p : withAliases(id->paths))
            if (toLower(baseName(p)) == expected)
                f.check = BiosCheck::Verified;
    }
    // Firmware de DS : jamais dans la base (il contient vos réglages), on le
    // reconnaît à son en-tête.
    std::string model;
    if (f.check != BiosCheck::Verified && expected == "firmware.bin" && !(model = dsFirmwareModel(m_dir / rel)).empty()) {
        f.check = BiosCheck::Verified;
        f.note = "Reconnu · " + model;
        return f;
    }
    switch (f.check) {
    case BiosCheck::Verified: f.note = region.empty() ? "Vérifié" : region + " · vérifié"; break;
    case BiosCheck::Other: f.note = "C'est en fait " + baseName(id->paths.front()); break;
    default: f.note = "Inconnu de la base"; break;
    }
    return f;
}

uint64_t BiosManager::folderSignature() const
{
    Hasher64 hasher;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        // Les fichiers que les cores écrivent eux-mêmes pendant le jeu
        // (mémoire NVRAM, caches de shaders...) ne comptent pas.
        const std::string ext = toLower(toUtf8(it->path().extension()));
        if (!it->is_regular_file(fileEc) || ext == ".nvm" || ext == ".mec" || ext == ".idx" || ext == ".bin_cache")
            continue;
        const std::string name = toUtf8(it->path());
        const uint64_t size = it->file_size(fileEc);
        const long long time = it->last_write_time(fileEc).time_since_epoch().count();
        hasher.update(name.data(), name.size());
        hasher.update(&size, sizeof(size));
        hasher.update(&time, sizeof(time));
    }
    return hasher.finish();
}

bool BiosManager::refreshIfChanged()
{
    if (folderSignature() == m_signature)
        return false;
    refresh();
    return true;
}

void BiosManager::refresh()
{
    m_signature = folderSignature();
    std::error_code ec;
    auto exists = [&](const fs::path& rel) { return fs::exists(m_dir / rel, ec); };

    m_systems.clear();

    // PlayStation 2 : n'importe quel BIOS de 4 Mo dans pcsx2/bios/ ; la base
    // donne sa version et sa région.
    BiosSystem ps2{"PlayStation 2", "ps2", 0x1E2A78, BiosNeed::Required, "Obligatoire", {}, false};
    for (auto it = fs::directory_iterator(m_dir / kPs2Dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->file_size(ec) != kPs2BiosSize)
            continue;
        BiosFile f{toUtf8(it->path().filename()), "Présent", true, BiosCheck::Verified};
        if (!m_db.empty()) {
            const std::optional<Identity> id = identify(it->path());
            if (id && id->system == "Sony - PlayStation 2") {
                f.label = ps2Version(id->paths.front());
                f.note = "Vérifié";
            } else if (const std::string version = ps2RomVer(it->path()); !version.empty()) {
                // Absent de la base, mais c'est bien un BIOS de PS2 (et PCSX2
                // le démarre) : on donne sa version, sans fausse alerte.
                f.label = ps2Version(version);
                f.note = "Reconnu · copie non répertoriée";
            } else {
                f.check = BiosCheck::Unknown;
                f.note = "Inconnu de la base";
            }
        }
        ps2.files.push_back(std::move(f));
    }
    if (ps2.files.empty())
        ps2.files.push_back({"BIOS de PlayStation 2 (fichier de 4 Mo)", "Manquant", false});
    m_systems.push_back(std::move(ps2));

    m_systems.push_back({"PlayStation", "ps1", 0x6B6E78, BiosNeed::Recommended, "Recommandé · quelques jeux en ont besoin",
                         {describe("scph5502.bin", "scph5502.bin", "Europe"),
                          describe("scph5501.bin", "scph5501.bin", "Amérique du Nord"),
                          describe("scph5500.bin", "scph5500.bin", "Japon")},
                         false});

    // GameCube / Wii : pas de BIOS de Nintendo, mais les fichiers libres de
    // Dolphin (polices, réglages par jeu, shaders), installables en un clic
    // depuis la fiche d'un jeu. Un dossier Sys à moitié vide ne suffit pas :
    // sans GameSettings, certains jeux plantent (Ultimate Spider-Man).
    BiosSystem gc{"GameCube / Wii", "gc wii", 0x5B3FA0, BiosNeed::Required,
                  "Obligatoire · fichiers libres de Dolphin (bouton Installer sur la fiche d'un jeu)", {}, false};
    const bool sys = exists(kDolphinSys / "GameSettings") && exists(kDolphinSys / "GC");
    const bool partial = !sys && exists(kDolphinSys);
    gc.files.push_back({"dolphin-emu/Sys", sys ? "Complet" : partial ? "Incomplet" : "Manquant", sys || partial,
                        sys ? BiosCheck::Verified : partial ? BiosCheck::Unknown : BiosCheck::Missing});
    m_systems.push_back(std::move(gc));

    // PSP : pas de BIOS (PPSSPP émule le système de la console), mais ses
    // fichiers libres : polices des textes, compat.ini (réglages par jeu).
    const bool ppsspp = exists(fs::path("PPSSPP") / "compat.ini") && exists(fs::path("PPSSPP") / "flash0");
    m_systems.push_back({"PSP", "psp", 0x334155, BiosNeed::Recommended,
                         "Pas de BIOS · fichiers libres de PPSSPP (bouton Installer sur la fiche d'un jeu)",
                         {{"PPSSPP", ppsspp ? "Complet" : "Manquant", ppsspp, ppsspp ? BiosCheck::Verified : BiosCheck::Missing}},
                         false});

    m_systems.push_back({"Nintendo DS", "nds", 0x475569, BiosNeed::Optional, "Facultatif · améliore la compatibilité",
                         {describe("bios7.bin", "bios7.bin", ""), describe("bios9.bin", "bios9.bin", ""),
                          describe("firmware.bin", "firmware.bin", "")},
                         false});
    m_systems.push_back({"Game Boy Advance", "gba", 0x0F766E, BiosNeed::Optional, "Facultatif · écran de démarrage",
                         {describe("gba_bios.bin", "gba_bios.bin", "")}, false});
    m_systems.push_back({"Dreamcast", "dc", 0xE85D04, BiosNeed::Recommended,
                         "Recommandé · sans lui, Flycast imite le BIOS (moins compatible)",
                         {describe(kDreamcastDir / "dc_boot.bin", "dc_boot.bin", ""),
                          describe(kDreamcastDir / "dc_flash.bin", "dc_flash.bin", "")},
                         false});
    // neogeo.zip change à chaque version de FBNeo : un fichier "inconnu" de
    // la base reste normal, on ne s'en inquiète pas.
    BiosFile neogeo = describe(kNeoGeoBios, "neogeo.zip", "");
    if (neogeo.check == BiosCheck::Unknown) {
        neogeo.check = BiosCheck::Verified;
        neogeo.note = "Présent";
    }
    m_systems.push_back({"Neo Geo", "neogeo", 0xA16207, BiosNeed::Required, "Obligatoire · neogeo.zip (romset FBNeo)",
                         {neogeo}, false});
    // Sega : le lecteur CD de la Mega Drive et la Saturn ont besoin du BIOS
    // de la console (celui d'une région suffit).
    m_systems.push_back({"Sega CD / Mega-CD", "segacd", 0x0F172A, BiosNeed::Required, "Obligatoire · un BIOS suffit (celui de la région du jeu)",
                         {describe("bios_CD_E.bin", "bios_CD_E.bin", "Europe"), describe("bios_CD_U.bin", "bios_CD_U.bin", "Amérique du Nord"),
                          describe("bios_CD_J.bin", "bios_CD_J.bin", "Japon")},
                         false});
    m_systems.push_back({"Sega Saturn", "saturn", 0x334155, BiosNeed::Required,
                         "Obligatoire pour Beetle Saturn · sans BIOS, Yabause (moins fidèle) prend le relais",
                         {describe("mpr-17933.bin", "mpr-17933.bin", "Europe / Amérique du Nord"),
                          describe("sega_101.bin", "sega_101.bin", "Japon")},
                         false});
    // Game Boy et Game Boy Color : l'écran de démarrage de la console (le
    // logo qui descend), facultatif.
    m_systems.push_back({"Game Boy · Game Boy Color", "gb gbc", 0x3F4A5A, BiosNeed::Optional, "Facultatif · écran de démarrage",
                         {describe("gb_bios.bin", "gb_bios.bin", "Game Boy"), describe("gbc_bios.bin", "gbc_bios.bin", "Game Boy Color")},
                         false});
    // Atari : la Lynx et la 5200 ne démarrent pas sans leur BIOS ; la 7800
    // s'en passe (il n'affiche que le logo).
    m_systems.push_back({"Atari Lynx", "lynx", 0xB45309, BiosNeed::Required, "Obligatoire",
                         {describe("lynxboot.img", "lynxboot.img", "")}, false});
    m_systems.push_back({"Atari 5200", "a5200", 0x78350F, BiosNeed::Required, "Obligatoire",
                         {describe("5200.rom", "5200.rom", "")}, false});
    m_systems.push_back({"Atari 7800", "a7800", 0x9A3412, BiosNeed::Optional, "Facultatif · écran de démarrage",
                         {describe("7800 BIOS (E).rom", "7800 BIOS (E).rom", "Europe"),
                          describe("7800 BIOS (U).rom", "7800 BIOS (U).rom", "Amérique du Nord")},
                         false});
    // 3DS : pas de BIOS, mais les jeux doivent être décryptés. Les clés de
    // la console (qui servent à décrypter) ne sont jamais fournies : illégal.
    m_systems.push_back({"Nintendo 3DS", "3ds", 0xB91C5C, BiosNeed::None,
                         "Pas de BIOS · jeux décryptés uniquement (vos propres dumps)", {}, true});
    m_systems.push_back({"Arcade · Capcom CPS1, CPS2, CPS3", "arcade", 0xC2410C, BiosNeed::None,
                         "Selon le jeu · les BIOS d'arcade (qsound.zip, sfiii...) sont dans les romsets, rangés avec les jeux",
                         {}, true});
    m_systems.push_back({"N64 · SNES · NES · Mega Drive · Atari 2600 · Jaguar", "n64 snes nes md a2600 jaguar", 0x1F7A47,
                         BiosNeed::None, "Aucun BIOS nécessaire", {}, true});

    for (BiosSystem& s : m_systems) {
        const bool any = std::any_of(s.files.begin(), s.files.end(), [](const BiosFile& f) { return f.present; });
        if (s.need == BiosNeed::Required)
            s.ready = any;
        else
            s.ready = true; // facultatif ou recommandé : la console démarre quand même
        // Dolphin : le dossier Sys doit être complet, pas seulement présent.
        if (std::string_view(s.systemIds) == "gc wii")
            s.ready = sys;
    }
}

const BiosSystem* BiosManager::forSystem(const char* systemId) const
{
    for (const BiosSystem& s : m_systems) {
        std::istringstream ids(s.systemIds);
        std::string id;
        while (ids >> id) {
            if (id == systemId)
                return &s;
        }
    }
    return nullptr;
}

// --- Rangement -------------------------------------------------------------------------

std::string BiosManager::import(const fs::path& source)
{
    std::error_code ec;
    std::vector<fs::path> targets;
    std::string system;
    bool verified = false;

    // 1. Par le contenu : le fichier est rangé sous TOUS les noms que lui
    //    donne la base (chaque core a ses habitudes : dmg_boot.bin, gb_bios.bin...).
    if (fs::file_size(source, ec) <= kMaxBiosSize)
        if (const std::optional<Identity> id = identify(source)) {
            for (const std::string& p : withAliases(id->paths))
                targets.push_back(m_dir / fromUtf8(p));
            system = shortSystem(id->system);
            verified = true;
        }

    // 2. Absent de la base, mais reconnu à son en-tête (BIOS PS2 non
    //    répertorié, firmware de DS avec vos réglages).
    if (targets.empty() && fs::file_size(source, ec) <= kMaxBiosSize)
        if (const std::optional<HeaderIdentity> id = identifyByHeader(source)) {
            targets.push_back(m_dir / id->target);
            system = id->label;
            verified = true;
        }

    // 3. Les fichiers qui accompagnent un BIOS de PS2 (réglages de la console,
    //    lecteur DVD) : PCSX2 les cherche à côté de lui, sous le même nom.
    if (targets.empty()) {
        const std::string ext = toLower(toUtf8(source.extension()));
        for (const char* companion : {".nvm", ".mec", ".erom", ".rom1", ".rom2"})
            if (ext == companion) {
                targets.push_back(m_dir / kPs2Dir / source.filename());
                system = "PlayStation 2 (" + ext.substr(1) + ")";
                verified = true;
            }
    }

    // 4. Sinon, par le nom (fichier absent de la base : version rare...).
    if (targets.empty()) {
        const std::string name = toLower(toUtf8(source.filename()));
        for (const Known& k : kKnown)
            if (name == k.file) {
                targets.push_back(m_dir / k.file);
                system = k.system;
            }
        if (name == "dc_boot.bin" || name == "dc_flash.bin") {
            targets.push_back(m_dir / kDreamcastDir / name);
            system = "Dreamcast";
        }
        if (name == "neogeo.zip") {
            targets.push_back(m_dir / kNeoGeoBios);
            system = "Neo Geo";
        }
        if (targets.empty() && fs::file_size(source, ec) == kPs2BiosSize) {
            targets.push_back(m_dir / kPs2Dir / source.filename());
            system = "PlayStation 2";
            // Sa carte d'identité interne suffit à le reconnaître.
            if (const std::string version = ps2RomVer(source); !version.empty()) {
                system = ps2Version(version);
                verified = true;
            }
        }
    }
    if (targets.empty())
        return "Fichier non reconnu comme BIOS : " + toUtf8(source.filename());

    for (const fs::path& target : targets) {
        fs::create_directories(target.parent_path(), ec);
        if (fs::equivalent(source, target, ec))
            continue; // déjà à sa place
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return "Copie impossible : " + ec.message();
    }
    refresh();
    // (neogeo.zip n'est jamais "vérifié" : il change à chaque version de FBNeo.)
    return "BIOS " + system + " ajouté" + (verified || system == "Neo Geo" ? "" : " (non vérifié : inconnu de la base)");
}

std::string BiosManager::importFolder(const fs::path& folder)
{
    // On ne garde que ce que la base reconnaît à coup sûr : dans un dossier
    // quelconque, un fichier "bios7.bin" peut très bien être autre chose.
    std::error_code ec;
    std::set<std::string> systems;
    int found = 0;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        // (Une erreur sur un fichier ne doit pas arrêter le parcours : son
        // propre code d'erreur, distinct de celui de l'itérateur.)
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || it->file_size(fileEc) > kMaxBiosSize)
            continue;
        const std::optional<Identity> id = identify(it->path());
        if (!id) {
            // Reconnu à son en-tête ? (BIOS PS2 non répertorié, firmware de DS)
            if (const std::optional<HeaderIdentity> header = identifyByHeader(it->path())) {
                const fs::path target = m_dir / header->target;
                fs::create_directories(target.parent_path(), fileEc);
                if (!fs::equivalent(it->path(), target, fileEc))
                    fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, fileEc);
                systems.insert(header->label);
                ++found;
            }
            continue;
        }
        for (const std::string& p : withAliases(id->paths)) {
            const fs::path target = m_dir / fromUtf8(p);
            fs::create_directories(target.parent_path(), fileEc);
            if (!fs::equivalent(it->path(), target, fileEc))
                fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, fileEc);
        }
        systems.insert(shortSystem(id->system));
        ++found;
    }
    refresh();
    if (found == 0)
        return "Aucun BIOS reconnu dans " + toUtf8(folder.filename());
    std::string list;
    for (const std::string& s : systems)
        list += (list.empty() ? "" : ", ") + s;
    return std::to_string(found) + (found > 1 ? " BIOS reconnus et rangés : " : " BIOS reconnu et rangé : ") + list;
}
