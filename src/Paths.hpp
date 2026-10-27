#pragma once
// Paths : où sont les fichiers du programme, selon le système.
//
//  - base() : le dossier des fichiers fournis avec Nexo Retro (assets/ :
//    polices, langues, logo, base des BIOS). Sous Windows, celui de l'exe.
//    Sous Android, ils sont DANS l'APK (une archive) : ils sont recopiés au
//    premier lancement dans le stockage privé de l'appli (voir android/Jni.cpp).
//  - data(dossier, nom) : un fichier ou dossier des données (roms, bios,
//    saves...). Une exception sous Android : les cores (.so). Android refuse
//    de charger du code depuis le stockage partagé (/sdcard) : ils restent
//    dans le stockage privé de l'appli.

#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>

namespace paths {

// Android : le stockage privé de l'appli (fixé au démarrage, voir main.cpp).
inline std::filesystem::path& privateDir()
{
    static std::filesystem::path dir;
    return dir;
}

inline std::filesystem::path base()
{
#ifdef __ANDROID__
    return privateDir();
#else
    return fromUtf8(SDL_GetBasePath());
#endif
}

inline std::filesystem::path data(const std::filesystem::path& dataDir, const char* name)
{
#ifdef __ANDROID__
    if (std::strcmp(name, "cores") == 0)
        return privateDir() / "cores";
#endif
    return dataDir / name;
}

} // namespace paths
