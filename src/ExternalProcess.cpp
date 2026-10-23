#include "ExternalProcess.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

// Windows reçoit UNE ligne de commande : chaque argument qui contient un
// espace est mis entre guillemets ("C:\Mes jeux\Zelda.nsp").
void appendArgument(std::wstring& line, const std::wstring& arg)
{
    if (!line.empty())
        line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    for (const wchar_t c : arg) {
        if (c == L'"')
            line += L'\\';
        line += c;
    }
    line += L'"';
}

} // namespace

ExternalProcess::~ExternalProcess()
{
    if (m_process)
        CloseHandle(static_cast<HANDLE>(m_process));
}

bool ExternalProcess::start(const std::filesystem::path& exe, const std::vector<std::string>& args, std::string& error)
{
    std::wstring line;
    appendArgument(line, exe.wstring());
    for (const std::string& a : args)
        appendArgument(line, widen(a));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const std::wstring folder = exe.parent_path().wstring();
    // [C++] CreateProcessW peut modifier la ligne de commande : il lui faut
    // un tampon modifiable, pas une chaîne constante.
    if (!CreateProcessW(exe.wstring().c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                        &info)) {
        error = "Windows n'a pas pu lancer le programme (erreur " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(info.hThread);
    if (m_process)
        CloseHandle(static_cast<HANDLE>(m_process));
    m_process = info.hProcess;
    return true;
}

bool ExternalProcess::running()
{
    if (!m_process)
        return false;
    if (WaitForSingleObject(static_cast<HANDLE>(m_process), 0) == WAIT_TIMEOUT)
        return true;
    CloseHandle(static_cast<HANDLE>(m_process));
    m_process = nullptr;
    return false;
}
