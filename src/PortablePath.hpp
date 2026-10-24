#pragma once
// PortablePath : écrire dans nos fichiers (userdata.txt, scraper/games.txt)
// des chemins qui restent valables quand le dossier des données change de
// place : une clé USB n'a pas la même lettre (E:, F:...) sur chaque PC.
//
//   D:\NexoRetro\roms\snes\Zelda.sfc  ->  .\roms\snes\Zelda.sfc   (dans le dossier des données)
//   D:\Jeux\GBA\Metroid.gba           ->  *:\Jeux\GBA\Metroid.gba (même disque que les données)
//   \\nas\jeux\Sonic.md               ->  \\nas\jeux\Sonic.md     (ailleurs : tel quel)
//
// Les anciens fichiers, aux chemins complets, se lisent toujours.

#include "Utf8.hpp"

#include <filesystem>
#include <string>

namespace portable {

// [C++] "inline" : une fonction définie dans un en-tête, incluse par
// plusieurs .cpp, sans erreur "définie deux fois" à l'édition des liens.
inline std::string store(const std::string& path, const std::filesystem::path& base)
{
    namespace fs = std::filesystem;
    if (path.empty() || base.empty())
        return path;
    const fs::path p = fromUtf8(path);
    const fs::path relative = p.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..")
        return toUtf8(fs::path(".") / relative);
    if (!p.root_name().empty() && p.root_name() == base.root_name() && toUtf8(p.root_name()).size() == 2)
        return "*:" + toUtf8(p.root_directory() / p.relative_path()); // (lettre de lecteur seulement)
    return path;
}

inline std::string load(const std::string& stored, const std::filesystem::path& base)
{
    if (base.empty())
        return stored;
    if (stored.size() > 2 && stored[0] == '.' && (stored[1] == '\\' || stored[1] == '/'))
        return toUtf8((base / fromUtf8(stored.substr(2))).lexically_normal());
    if (stored.size() > 2 && stored[0] == '*' && stored[1] == ':')
        return toUtf8(base.root_name()) + stored.substr(2);
    return stored;
}

// Le dossier de référence : complet et normalisé, comme les clés de UserData::keyOf.
inline std::filesystem::path baseOf(const std::filesystem::path& dataDir)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(dataDir, ec);
    return (ec ? dataDir : absolute).lexically_normal();
}

} // namespace portable
