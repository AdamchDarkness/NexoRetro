#include "SaveFiles.hpp"

#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <system_error>

namespace fs = std::filesystem;

SaveFiles saveFilesFor(const fs::path& savesDir, const std::string& stem)
{
    SaveFiles files;
    files.dir = savesDir / fromUtf8(stem);
    files.sram = files.dir / fromUtf8(stem + ".srm");
    files.state = files.dir / fromUtf8(stem + ".state");
    files.autoState = files.dir / fromUtf8(stem + ".auto.state");

    // Déménagement des fichiers de l'ancienne organisation (tout dans saves/).
    std::error_code ec;
    for (const fs::path* file : {&files.sram, &files.state, &files.autoState}) {
        const fs::path old = savesDir / file->filename();
        if (!fs::is_regular_file(old, ec) || fs::exists(*file, ec))
            continue;
        fs::create_directories(files.dir, ec);
        fs::rename(old, *file, ec);
        if (!ec)
            SDL_Log("Sauvegarde rangée dans son dossier : %s", toUtf8(*file).c_str());
    }
    return files;
}
