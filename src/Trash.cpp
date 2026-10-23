#include "Trash.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

bool moveToTrash(const std::vector<std::filesystem::path>& files, std::string& error)
{
    if (files.empty())
        return true;
    // SHFileOperationW attend une liste de chemins séparés par des '\0' et
    // terminée par un double '\0' (une vieille convention de Windows).
    std::wstring list;
    for (const std::filesystem::path& f : files) {
        list += std::filesystem::absolute(f).wstring();
        list += L'\0';
    }
    list += L'\0';

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = list.c_str();
    // ALLOWUNDO : vers la Corbeille (et non effacé). Les autres options :
    // pas de question ni de fenêtre de progression de Windows, on a déjà
    // demandé confirmation dans notre interface.
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&op);
    if (result != 0 || op.fAnyOperationsAborted) {
        error = "Windows a refusé (code " + std::to_string(result) + ") : fichier ouvert ailleurs ou protégé ?";
        return false;
    }
    return true;
}
