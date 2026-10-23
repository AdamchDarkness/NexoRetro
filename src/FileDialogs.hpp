#pragma once
// FileDialogs : les fenêtres "Ouvrir un fichier" / "Choisir un dossier" de
// Windows, via SDL.
//
// Ces fenêtres sont asynchrones : SDL nous rappelle plus tard, parfois
// depuis un autre fil d'exécution (thread). Les résultats sont donc rangés
// dans une file protégée par un mutex, puis lus par la boucle principale.

#include <SDL3/SDL.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// Ce que l'utilisateur voulait faire en ouvrant la fenêtre.
enum class DialogPurpose { AddGameFiles, AddGameFolder, AddBiosFile, AddBiosFolder, ChooseCover, ImportSave, ChooseExternal, ImportSystemFiles,
                           ChooseBackground, ChooseFont, ExportBios, ImportBios };

struct DialogResult {
    DialogPurpose purpose;
    std::vector<std::filesystem::path> paths; // vide si annulé
};

class FileDialogs {
public:
    void openFiles(SDL_Window* window, DialogPurpose purpose, bool images, bool multiple);
    void openFolder(SDL_Window* window, DialogPurpose purpose);
    // Une police (.ttf, .otf).
    void openFonts(SDL_Window* window, DialogPurpose purpose);
    // Une archive .zip à ouvrir, ou à créer ("Enregistrer sous", avec un nom
    // proposé).
    void openZip(SDL_Window* window, DialogPurpose purpose);
    void saveZip(SDL_Window* window, DialogPurpose purpose, const std::string& suggestedName);

    // Les résultats arrivés depuis le dernier appel.
    std::vector<DialogResult> poll();

private:
    struct Request {
        FileDialogs* owner;
        DialogPurpose purpose;
    };
    static void SDLCALL onResult(void* userdata, const char* const* files, int filter);

    std::mutex m_mutex;
    std::vector<DialogResult> m_results;
};
