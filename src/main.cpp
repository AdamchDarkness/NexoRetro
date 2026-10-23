// Point d'entrée : lit la ligne de commande et lance l'application.
//
//   emulateur                         -> ouvre la bibliothèque
//   emulateur <core.dll> [jeu]        -> lance directement un jeu

#include "App.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void printUsage()
{
    std::puts(
        "Usage : emulateur [core.dll [jeu]] [--data <dossier>]\n"
        "        [--frames <n> [--screenshot <fichier.bmp>]]   (tests automatiques)\n"
        "        [--screen home|library|game|covers|add|bios|friends|settings|pads|infos] [--menu]\n"
        "        [--clavier | --manette N]   (deux émulateurs sur le même PC)\n"
        "\n"
        "Sans argument, ouvre la bibliothèque de jeux. Le dossier de données\n"
        "(cores, bios, roms, saves) est par défaut celui de l'exécutable.");
}

bool parseArgs(int argc, char** argv, AppOptions& out)
{
    for (int i = 1; i < argc; ++i) {
        // [C++] std::string_view : une "vue" sur une chaîne existante, sans
        // copie. Permet de comparer avec == au lieu de strcmp().
        // SDL nous donne les arguments en UTF-8, d'où fromUtf8().
        const std::string_view arg = argv[i];
        if (arg == "--data" && i + 1 < argc)
            out.dataDir = fromUtf8(argv[++i]);
        else if (arg == "--frames" && i + 1 < argc)
            out.maxFrames = std::atoll(argv[++i]);
        else if (arg == "--screenshot" && i + 1 < argc)
            out.exitScreenshot = fromUtf8(argv[++i]);
        else if (arg == "--settings-tab" && i + 1 < argc)
            out.settingsTab = std::atoi(argv[++i]);
        else if (arg == "--screen" && i + 1 < argc)
            out.startScreen = argv[++i];
        else if (arg == "--menu")
            out.openMenu = true;
        else if (arg == "--net-host" && i + 1 < argc)
            out.netTestHost = fromUtf8(argv[++i]);
        else if (arg == "--net-join" && i + 1 < argc)
            out.netTestJoin = fromUtf8(argv[++i]);
        else if (arg == "--net-password" && i + 1 < argc)
            out.netTestPassword = argv[++i];
        else if (arg == "--net-expect" && i + 1 < argc)
            out.netTestExpect = std::atoi(argv[++i]);
        else if (arg == "--net-game")
            out.netTestGame = true;
        else if (arg == "--net-play" && i + 1 < argc)
            out.netTestPlayFrames = std::atoi(argv[++i]);
        else if (arg == "--net-latency" && i + 1 < argc)
            out.simulatedLatencyMs = std::max(0, std::atoi(argv[++i]));
        else if (arg == "--test-resume")
            out.testResume = true;
        else if (arg == "--controls")
            out.openControls = true;
        else if (arg == "--test-play")
            out.testPlay = true;
        else if (arg == "--net-link")
            out.netTestLink = true;
        else if (arg == "--net-start" && i + 1 < argc)
            out.netTestStart = std::clamp(std::atoi(argv[++i]), 0, 3);
        else if (arg == "--test-remove" && i + 1 < argc)
            out.testRemove = argv[++i];
        else if (arg == "--import-bios" && i + 1 < argc)
            out.importBios = fromUtf8(argv[++i]);
        else if (arg == "--install-external" && i + 1 < argc)
            out.installExternal = argv[++i];
        else if (arg == "--import-system" && i + 2 < argc) {
            out.importSystemId = argv[++i];
            out.importSystemDir = fromUtf8(argv[++i]);
        }
        else if (arg == "--clavier")
            out.inputSource = Input::Source::KeyboardOnly;
        else if (arg == "--manette" && i + 1 < argc) {
            out.inputSource = Input::Source::Gamepad;
            out.inputPad = std::max(0, std::atoi(argv[++i]) - 1); // l'utilisateur compte à partir de 1
        }
        else if (arg == "-h" || arg == "--help")
            return false;
        else if (out.core.empty())
            out.core = fromUtf8(arg);
        else if (out.rom.empty())
            out.rom = fromUtf8(arg);
        else
            return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    // La console Windows n'affiche pas l'UTF-8 par défaut (accents illisibles).
    SetConsoleOutputCP(CP_UTF8);
#endif

    AppOptions options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 1;
    }
    if (options.dataDir.empty())
        options.dataDir = fromUtf8(SDL_GetBasePath()); // dossier de l'exécutable
    // Une fenêtre qui n'écoute qu'une manette doit la lire même quand elle
    // n'est pas au premier plan (l'autre fenêtre a le clavier).
    if (options.inputSource == Input::Source::Gamepad)
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init a échoué : %s", SDL_GetError());
        return 1;
    }

    int code = 0;
    {
        // [C++] Ce bloc { } garantit que l'App (fenêtre, contexte OpenGL...)
        // est détruite AVANT SDL_Quit().
        App app(std::move(options));
        code = app.run();
    }
    SDL_Quit();
    return code;
}
