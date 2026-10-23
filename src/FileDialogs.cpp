#include "FileDialogs.hpp"

#include "Utf8.hpp"

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

void FileDialogs::openFolder(SDL_Window* window, DialogPurpose purpose)
{
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
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DialogResult> results;
    results.swap(m_results);
    return results;
}
