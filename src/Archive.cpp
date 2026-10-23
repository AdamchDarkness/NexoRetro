#include "Archive.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Lance un programme sans fenêtre et attend qu'il se termine. Renvoie son
// code de sortie (-1 s'il n'a pas démarré).
// (CREATE_NO_WINDOW : Nexo Retro n'a pas de terminal, et un terminal qui
// clignote le temps de la commande ferait peur.)
int runHidden(const std::wstring& commandLine)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring line = commandLine; // CreateProcessW peut modifier la chaîne
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// "C:\Windows\System32\tar.exe", entre guillemets : le chemin complet, pour
// ne jamais lancer un autre tar.exe trouvé dans le PATH.
std::wstring tar()
{
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    return L"\"" + std::wstring(system) + L"\\tar.exe\"";
}

std::wstring quoted(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

} // namespace

namespace archive {

bool extract(const fs::path& file, const fs::path& dest)
{
    std::error_code ec;
    fs::create_directories(dest, ec);
    return runHidden(tar() + L" -xf " + quoted(file) + L" -C " + quoted(dest)) == 0;
}

bool zipFolder(const fs::path& folder, const fs::path& file)
{
    std::error_code ec;
    fs::remove(file, ec); // tar ne remplace pas une archive existante
    // -a : le format vient de l'extension (.zip) ; -C : on se place dans le
    // dossier, et "." prend tout son contenu.
    return runHidden(tar() + L" -a -cf " + quoted(file) + L" -C " + quoted(folder) + L" .") == 0 &&
           fs::exists(file, ec);
}

} // namespace archive
