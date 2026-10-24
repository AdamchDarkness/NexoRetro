#include "Archive.hpp"

#include "Utf8.hpp"

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

// Pareil, mais en récupérant ce que le programme écrit (sa sortie standard).
// [Windows] Un "tuyau" (pipe) : le programme écrit d'un côté, on lit de
// l'autre. On lit pendant qu'il tourne : un tuyau plein le bloquerait.
std::string runCaptured(const std::wstring& commandLine, int& code)
{
    code = -1;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; // TRUE : le programme lancé hérite du tuyau
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
        return {};
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0); // notre côté, lui, ne s'hérite pas
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring line = commandLine;
    const BOOL started =
        CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd); // sinon la lecture n'aurait jamais de fin
    std::string output;
    if (started) {
        char buffer[4096];
        DWORD got = 0;
        while (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) && got > 0)
            output.append(buffer, got);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        code = static_cast<int>(exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(readEnd);
    return output;
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

std::vector<fs::path> list(const fs::path& file)
{
    // "tar -tf" : une ligne par élément ; les dossiers finissent par "/".
    // (Les accents des noms peuvent être mal décodés : ils ne servent ici
    // qu'à reconnaître des extensions, c'est sans importance.)
    int code = 0;
    const std::string output = runCaptured(tar() + L" -tf " + quoted(file), code);
    std::vector<fs::path> files;
    if (code != 0)
        return files;
    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();
        std::string name = output.substr(start, end - start);
        if (!name.empty() && name.back() == '\r')
            name.pop_back();
        if (!name.empty() && name.back() != '/' && name.back() != '\\')
            files.push_back(fromUtf8(name));
        start = end + 1;
    }
    return files;
}

} // namespace archive
