#pragma once
// Conversions entre chemins de fichiers et chaînes UTF-8.
//
// Sous Windows, path.string() convertit vers l'ancienne page de code du
// système et peut échouer sur certains caractères ("Pokémon" passe, mais pas
// un titre japonais). SDL, ImGui et les cores libretro utilisent tous de
// l'UTF-8 : on passe donc toujours par ces deux fonctions.

#include <filesystem>
#include <string>
#include <string_view>

inline std::string toUtf8(const std::filesystem::path& path)
{
    // [C++20] u8string() renvoie un std::u8string (des char8_t) qu'on recopie
    // en std::string (des char) : même octets, type différent.
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

inline std::filesystem::path fromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
