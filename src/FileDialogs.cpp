#include "FileDialogs.hpp"

#include "Utf8.hpp"
#ifdef __ANDROID__
#include "android/Jni.hpp"
#endif

void FileDialogs::openFiles(SDL_Window* window, DialogPurpose purpose, bool images, bool multiple)
{
    static const SDL_DialogFileFilter imageFilter[] = {{"Images", "png;jpg;jpeg"}};
    // [C++] "new" : la requête doit survivre jusqu'au rappel de SDL ; c'est
    // onResult() qui la libère avec "delete".
    auto* request = new Request{this, purpose};
    SDL_ShowOpenFileDialog(&FileDialogs::onResult, request, window, images ? imageFilter : nullptr, images ? 1 : 0,
                           nullptr, multiple);
}

void FileDialogs::openFonts(SDL_Window* window, DialogPurpose purpose)
{
    static const SDL_DialogFileFilter fontFilter[] = {{"Polices", "ttf;otf"}};
    auto* request = new Request{this, purpose};
    SDL_ShowOpenFileDialog(&FileDialogs::onResult, request, window, fontFilter, 1, nullptr, false);
}

namespace {
const SDL_DialogFileFilter kZipFilter[] = {{"Archive zip", "zip"}};
}

void FileDialogs::openZip(SDL_Window* window, DialogPurpose purpose)
{
    auto* request = new Request{this, purpose};
    SDL_ShowOpenFileDialog(&FileDialogs::onResult, request, window, kZipFilter, 1, nullptr, false);
}

void FileDialogs::openFile(SDL_Window* window, DialogPurpose purpose, const char* filterName, const char* extension)
{
    // (Le filtre doit vivre jusqu'à la réponse : il est rangé dans la requête.)
    auto* request = new Request{this, purpose, {filterName, extension}};
    SDL_ShowOpenFileDialog(&FileDialogs::onResult, request, window, &request->filter, 1, nullptr, false);
}

void FileDialogs::saveZip(SDL_Window* window, DialogPurpose purpose, const std::string& suggestedName)
{
    saveFile(window, purpose, suggestedName, "Archive zip", "zip");
}

void FileDialogs::saveFile(SDL_Window* window, DialogPurpose purpose, const std::string& suggestedName,
                           const char* filterName, const char* extension)
{
#ifdef __ANDROID__
    // Android : "Enregistrer sous" donnerait une adresse "content://..." où
    // le programme ne sait pas écrire. Le fichier va dans le dossier Exports
    // de Nexo Retro, que l'utilisateur retrouve avec son gestionnaire de fichiers.
    (void)window, (void)filterName, (void)extension;
    std::error_code ec;
    const std::filesystem::path exports = android::dataDir() / "Exports";
    std::filesystem::create_directories(exports, ec);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_results.push_back({purpose, {exports / fromUtf8(suggestedName)}});
    return;
#endif
    // (SDL copie le nom proposé ; le filtre, lui, doit vivre jusqu'à la
    // réponse : il est rangé dans la requête.)
    auto* request = new Request{this, purpose, {filterName, extension}};
    SDL_ShowSaveFileDialog(&FileDialogs::onResult, request, window, &request->filter, 1, suggestedName.c_str());
}

void FileDialogs::openFolder(SDL_Window* window, DialogPurpose purpose)
{
#ifdef __ANDROID__
    // Android n'a pas de fenêtre "Choisir un dossier" : on choisit plusieurs
    // fichiers à la place (pour les jeux et les BIOS).
    if (purpose == DialogPurpose::AddGameFolder || purpose == DialogPurpose::AddBiosFolder) {
        openFiles(window, purpose == DialogPurpose::AddGameFolder ? DialogPurpose::AddGameFiles : DialogPurpose::AddBiosFile, false, true);
        return;
    }
#endif
    auto* request = new Request{this, purpose};
    SDL_ShowOpenFolderDialog(&FileDialogs::onResult, request, window, nullptr, false);
}

void SDLCALL FileDialogs::onResult(void* userdata, const char* const* files, int)
{
    auto* request = static_cast<Request*>(userdata);
    DialogResult result{request->purpose, {}};
    for (const char* const* f = files; f && *f; ++f)
        result.paths.push_back(fromUtf8(*f));
    {
        // [C++] std::lock_guard verrouille le mutex et le déverrouille tout
        // seul à la fin du bloc (encore le RAII).
        std::lock_guard<std::mutex> lock(request->owner->m_mutex);
        request->owner->m_results.push_back(std::move(result));
    }
    delete request;
}

std::vector<DialogResult> FileDialogs::poll()
{
    std::vector<DialogResult> results;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        results.swap(m_results);
    }
#ifdef __ANDROID__
    // Android : le sélecteur donne des adresses "content://...", pas des
    // chemins. On recopie chaque document : les jeux (et les images ou polices
    // choisies) dans le dossier "Importés", qu'ils gardent ; le reste (BIOS,
    // sauvegarde, splits...), recopié ailleurs ensuite, dans un dossier de passage.
    for (DialogResult& result : results) {
        const bool keep = result.purpose == DialogPurpose::AddGameFiles || result.purpose == DialogPurpose::ChooseBackground ||
                          result.purpose == DialogPurpose::ChooseFont;
        const std::filesystem::path dest = keep ? android::dataDir() / "Importés" : android::privateDir() / "import";
        std::vector<std::filesystem::path> copied;
        for (const std::filesystem::path& path : result.paths) {
            const std::string text = toUtf8(path);
            if (text.rfind("content://", 0) != 0) {
                copied.push_back(path);
                continue;
            }
            const std::filesystem::path local = android::copyDocument(text, dest);
            if (!local.empty())
                copied.push_back(local);
            else
                SDL_Log("Import impossible : %s", text.c_str());
        }
        result.paths = std::move(copied);
    }
#endif
    return results;
}
