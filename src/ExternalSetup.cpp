#include "ExternalSetup.hpp"

#include "Utf8.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace external {

namespace {

std::string toLower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool nonEmptyDir(const fs::path& dir)
{
    std::error_code ec;
    return fs::is_directory(dir, ec) && fs::directory_iterator(dir, ec) != fs::directory_iterator();
}

// --- Switch ------------------------------------------------------------------------------

// Où l'émulateur Switch (Ryujinx et ses successeurs) cherche ses clés : dans
// "portable/system" à côté du programme s'il est en mode portable, sinon
// dans %APPDATA%\Ryujinx\system.
fs::path switchSystemDir(const fs::path& exe)
{
    std::error_code ec;
    if (!exe.empty() && fs::is_directory(exe.parent_path() / "portable", ec))
        return exe.parent_path() / "portable" / "system";
    const char* appData = SDL_getenv("APPDATA");
    return appData ? fromUtf8(appData) / "Ryujinx" / "system" : fs::path();
}

// --- Xbox --------------------------------------------------------------------------------

const fs::path kXboxDir = "xbox";
constexpr std::uintmax_t kMcpxSize = 512;               // la ROM de démarrage cachée dans la puce MCPX
constexpr std::uintmax_t kFlashSizes[] = {262144, 1048576}; // le BIOS "flash" (256 Ko ou 1 Mo)

struct XboxFiles {
    fs::path mcpx, flash, hdd;
};

XboxFiles xboxFiles(const fs::path& biosDir)
{
    XboxFiles x;
    std::error_code ec;
    for (auto it = fs::directory_iterator(biosDir / kXboxDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc))
            continue;
        const auto size = it->file_size(fileEc);
        if (toLower(toUtf8(it->path().extension())) == ".qcow2")
            x.hdd = it->path();
        else if (size == kMcpxSize)
            x.mcpx = it->path();
        else if (std::find(std::begin(kFlashSizes), std::end(kFlashSizes), size) != std::end(kFlashSizes))
            x.flash = it->path();
    }
    return x;
}

// xemu lit ses réglages dans xemu.toml, à côté de xemu.exe (mode portable).
// On n'écrit que les trois chemins de [sys.files] : le reste des réglages de
// l'utilisateur est conservé.
void configureXemu(const fs::path& exe, const fs::path& biosDir)
{
    if (exe.empty())
        return;
    const XboxFiles x = xboxFiles(biosDir);
    const fs::path toml = exe.parent_path() / "xemu.toml";
    std::vector<std::string> lines;
    {
        std::ifstream in(toml);
        std::string line;
        while (std::getline(in, line)) {
            const std::string t = line.substr(std::min(line.size(), line.find_first_not_of(" \t")));
            if (t.starts_with("bootrom_path") || t.starts_with("flashrom_path") || t.starts_with("hdd_path"))
                continue; // remplacés ci-dessous
            lines.push_back(line);
        }
    }
    // [C++] En TOML, une chaîne entre apostrophes est prise telle quelle :
    // pas besoin de doubler les \ des chemins Windows.
    std::vector<std::string> paths;
    if (!x.mcpx.empty())
        paths.push_back("bootrom_path = '" + toUtf8(x.mcpx) + "'");
    if (!x.flash.empty())
        paths.push_back("flashrom_path = '" + toUtf8(x.flash) + "'");
    if (!x.hdd.empty())
        paths.push_back("hdd_path = '" + toUtf8(x.hdd) + "'");
    auto section = std::find(lines.begin(), lines.end(), "[sys.files]");
    if (section == lines.end()) {
        lines.push_back("");
        lines.push_back("[sys.files]");
        section = lines.end() - 1;
    }
    lines.insert(section + 1, paths.begin(), paths.end());
    std::ofstream out(toml);
    for (const std::string& line : lines)
        out << line << "\n";
}

// Lance un programme sans attendre (RPCS3 qui installe le firmware).
bool launchDetached(const fs::path& exe, const std::wstring& arguments)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring line = L"\"" + exe.wstring() + L"\" " + arguments;
    const std::wstring dir = exe.parent_path().wstring();
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

SystemFiles systemFiles(const std::string& systemId, const fs::path& exe, const fs::path& biosDir)
{
    SystemFiles f;
    std::error_code ec;
    if (systemId == "ps3") {
        f.needed = true;
        f.label = "Firmware PS3";
        // RPCS3 installe le firmware dans dev_flash/, à côté de lui.
        f.ready = !exe.empty() && nonEmptyDir(exe.parent_path() / "dev_flash" / "vsh" / "module");
        f.status = exe.empty() ? "Installez d'abord RPCS3" : f.ready ? "Installé" : "Manquant : PS3UPDAT.PUP";
        f.help = "Téléchargez PS3UPDAT.PUP sur le site de Sony (gratuit), puis choisissez-le ici.";
        f.officialPage = "https://www.playstation.com/fr-fr/support/hardware/ps3/system-software/";
        f.officialLabel = "Site de Sony";
    } else if (systemId == "switch") {
        f.needed = true;
        f.label = "Clés de la Switch";
        const fs::path dir = switchSystemDir(exe);
        f.ready = !dir.empty() && fs::is_regular_file(dir / "prod.keys", ec);
        f.status = f.ready ? "prod.keys en place" : "Manquant : prod.keys";
        f.help = "À extraire de votre propre console (avec Lockpick_RCM). Le firmware s'installe ensuite depuis "
                 "le menu Outils de l'émulateur.";
    } else if (systemId == "xbox") {
        f.needed = true;
        f.label = "BIOS et disque dur Xbox";
        const XboxFiles x = xboxFiles(biosDir);
        f.ready = !x.mcpx.empty() && !x.flash.empty() && !x.hdd.empty();
        std::string missing;
        if (x.mcpx.empty())
            missing += "MCPX (512 octets)";
        if (x.flash.empty())
            missing += std::string(missing.empty() ? "" : ", ") + "BIOS flash";
        if (x.hdd.empty())
            missing += std::string(missing.empty() ? "" : ", ") + "disque .qcow2";
        f.status = f.ready ? "En place" : "Manquant : " + missing;
        f.help = "Le BIOS vient de votre console. Le disque dur vierge est proposé par xemu sur sa page officielle.";
        f.officialPage = "https://xemu.app/docs/required-files/";
        f.officialLabel = "Page de xemu";
    } else if (systemId == "ps4") {
        f.label = "Modules système PS4";
        f.status = "Facultatifs";
        f.ready = true;
    } else if (systemId == "x360") {
        f.label = "Fichiers système";
        f.status = "Aucun nécessaire";
        f.ready = true;
    }
    return f;
}

std::string importSystemFiles(const std::string& systemId, const fs::path& exe, const fs::path& biosDir,
                              const std::vector<fs::path>& files)
{
    std::error_code ec;
    if (systemId == "ps3") {
        if (exe.empty())
            return "Installez d'abord RPCS3";
        for (const fs::path& file : files)
            if (toLower(toUtf8(file.extension())) == ".pup") {
                // RPCS3 sait installer un firmware tout seul : --installfw.
                if (!launchDetached(exe, L"--installfw \"" + file.wstring() + L"\""))
                    return "Impossible de lancer RPCS3";
                return "RPCS3 installe le firmware : suivez sa fenêtre, puis fermez-la";
            }
        return "Choisissez le fichier PS3UPDAT.PUP";
    }
    if (systemId == "switch") {
        const fs::path dir = switchSystemDir(exe);
        int copied = 0;
        for (const fs::path& file : files) {
            const std::string name = toLower(toUtf8(file.filename()));
            if (name != "prod.keys" && name != "title.keys")
                continue;
            fs::create_directories(dir, ec);
            fs::copy_file(file, dir / name, fs::copy_options::overwrite_existing, ec);
            copied += !ec;
        }
        return copied ? "Clés de la Switch rangées dans " + toUtf8(dir) : "Choisissez prod.keys (et title.keys)";
    }
    if (systemId == "xbox") {
        int copied = 0;
        for (const fs::path& file : files) {
            const auto size = fs::file_size(file, ec);
            const bool hdd = toLower(toUtf8(file.extension())) == ".qcow2";
            const bool flash = std::find(std::begin(kFlashSizes), std::end(kFlashSizes), size) != std::end(kFlashSizes);
            if (!hdd && size != kMcpxSize && !flash)
                continue;
            fs::create_directories(biosDir / kXboxDir, ec);
            fs::copy_file(file, biosDir / kXboxDir / file.filename(), fs::copy_options::overwrite_existing, ec);
            copied += !ec;
        }
        configureXemu(exe, biosDir);
        return copied ? std::to_string(copied) + " fichier(s) Xbox rangé(s) dans bios/xbox, xemu est réglé"
                      : "Aucun fichier Xbox reconnu (MCPX de 512 octets, BIOS de 256 Ko ou 1 Mo, disque .qcow2)";
    }
    return "Rien à importer pour cette console";
}

void configure(const std::string& systemId, const fs::path& exe, const fs::path& biosDir)
{
    if (systemId == "xbox")
        configureXemu(exe, biosDir);
}

} // namespace external
