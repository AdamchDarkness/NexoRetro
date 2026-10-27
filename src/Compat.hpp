#pragma once
// Compat : les quelques fonctions de la bibliothèque C/C++ qui n'ont pas le
// même nom sous Windows et ailleurs (Android).
//
// Le code est écrit avec les noms de Microsoft (localtime_s, gmtime_s...) ;
// ailleurs, ce fichier les fournit en appelant leurs équivalents POSIX.

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#ifndef _WIN32
// [C] Mêmes paramètres que chez Microsoft (résultat d'abord) ; POSIX les
// donne dans l'autre ordre : localtime_r(source, résultat).
inline int localtime_s(std::tm* out, const std::time_t* time)
{
    return localtime_r(time, out) ? 0 : 1;
}

inline int gmtime_s(std::tm* out, const std::time_t* time)
{
    return gmtime_r(time, out) ? 0 : 1;
}

// La version « sûre » de sscanf ne change rien tant qu'on ne lit que des nombres.
#define sscanf_s sscanf
#endif

namespace compat {

// La date d'un fichier convertie vers l'horloge système (celle de time_t).
// [C++20] clock_cast n'existe pas encore dans la bibliothèque d'Android (libc++),
// qui a en revanche file_clock::to_sys.
inline std::chrono::system_clock::time_point toSystemClock(std::filesystem::file_time_type time)
{
#ifdef _WIN32
    return std::chrono::clock_cast<std::chrono::system_clock>(time);
#else
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(time));
#endif
}

} // namespace compat
