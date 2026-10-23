// Point d'entrée : lit la ligne de commande et lance l'application.
//
//   NexoRetro                         -> ouvre la bibliothèque
//   NexoRetro <core.dll> [jeu]        -> lance directement un jeu

#include "App.hpp"
#include "Utf8.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath : les dossiers spéciaux de Windows
#endif

namespace {

namespace fs = std::filesystem;

// Où ranger les données (cores/, bios/, roms/, saves/, settings.ini,
// userdata.txt...) quand --data n'est pas donné.
//
// Deux cas, reconnus grâce à de petits fichiers « marqueurs » posés à côté
// de NexoRetro.exe :
//
//  - copie INSTALLÉE (l'installateur écrit installed.txt) : le programme
//    vit dans %LOCALAPPDATA%\Programs\NexoRetro, un dossier que les mises
//    à jour remplacent. Les données vont donc ailleurs, dans
//    %APPDATA%\NexoRetro, où une mise à jour ou une désinstallation ne les
//    touche pas ;
//  - copie PORTABLE (le .zip, ou une compilation de développement : aucun
//    marqueur) : tout reste à côté de l'exécutable, comme avant. On peut
//    emporter le dossier sur une clé USB.
//
// Un portable.txt à côté de l'exe force le mode portable, même dans une
// copie installée (pour qui préfère tout garder au même endroit).
fs::path defaultDataDir()
{
    const fs::path exeDir = fromUtf8(SDL_GetBasePath()); // dossier de l'exécutable
#ifdef _WIN32
    // [C++] std::error_code : la version de exists() qui ne lance pas
    // d'exception en cas de problème (droits, disque débranché...) ; elle
    // remplit simplement le code d'erreur, qu'on ignore ici (=> false).
    std::error_code erreur;
    const bool installee = fs::exists(exeDir / "installed.txt", erreur);
    const bool portable = fs::exists(exeDir / "portable.txt", erreur);
    if (installee && !portable) {
        // FOLDERID_RoamingAppData = %APPDATA% (C:\Users\<nom>\AppData\Roaming).
        // Windows alloue la chaîne : c'est à nous de la libérer (CoTaskMemFree).
        PWSTR appData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
            const fs::path dossier = fs::path(appData) / L"NexoRetro";
            CoTaskMemFree(appData);
            fs::create_directories(dossier, erreur); // créé au premier lancement
            if (!erreur)
                return dossier;
        }
        else {
            CoTaskMemFree(appData); // sans effet sur nullptr, mais la règle est de toujours libérer
        }
        // Dossier introuvable ou impossible à créer : on retombe sur le
        // comportement portable plutôt que de refuser de démarrer.
    }
#endif
    return exeDir;
}

void printUsage()
{
    std::puts(
        "Usage : NexoRetro [core.dll [jeu]] [--data <dossier>]\n"
        "        [--frames <n> [--screenshot <fichier.bmp>]]   (tests automatiques)\n"
        "        [--screen home|library|game|covers|add|bios|friends|settings|pads|infos] [--menu]\n"
        "        [--clavier | --manette N]   (deux émulateurs sur le même PC)\n"
        "        [--console]   (affiche le journal dans un terminal ; il est aussi dans NexoRetro.log)\n"
        "\n"
        "Sans argument, ouvre la bibliothèque de jeux. Le dossier de données\n"
        "(cores, bios, roms, saves) est par défaut celui de l'exécutable, ou\n"
        "%APPDATA%\\NexoRetro pour une copie installée (installed.txt).");
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
        else if (arg == "--big-picture")
            out.bigPicture = true;
        else if (arg == "--library-view" && i + 1 < argc)
            out.libraryView = std::atoi(argv[++i]);
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
        else if (arg == "--console")
            ; // (déjà traité par setupConsole, au tout début de main)
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
        else if (arg == "--export-bios" && i + 1 < argc)
            out.exportBios = fromUtf8(argv[++i]);
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

// Nexo Retro est une application fenêtrée : Windows ne lui donne pas de
// terminal. Les messages (SDL_Log) vont quand même :
// - dans le tuyau ou le fichier choisi par le programme qui nous lance
//   (tests automatiques : "NexoRetro.exe ... | grep ...") ;
// - dans un terminal, seulement si on le demande avec --console (débogage) ;
// - et toujours dans NexoRetro.log, dans le dossier des données (voir startLogFile).
void setupConsole(int argc, char** argv)
{
#ifdef _WIN32
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const bool redirected = err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN;
    bool wanted = false;
    for (int i = 1; i < argc; ++i)
        wanted = wanted || std::string_view(argv[i]) == "--console";
    if (!redirected && wanted) {
        // Le terminal d'où on a été lancé, sinon un nouveau.
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            AllocConsole();
        // [C++] freopen rebranche stdout/stderr (les FILE* du C) sur le terminal.
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    // Le terminal de Windows n'affiche pas l'UTF-8 par défaut (accents illisibles).
    SetConsoleOutputCP(CP_UTF8);
#else
    (void)argc;
    (void)argv;
#endif
}

// Le journal : chaque message est aussi écrit dans <données>/NexoRetro.log
// (remplacé à chaque lancement). C'est lui qu'on demande à quelqu'un dont
// le jeu ne se lance pas.
FILE* g_logFile = nullptr;
SDL_LogOutputFunction g_defaultLog = nullptr;

void SDLCALL logToFile(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    if (g_logFile) {
        std::fputs(message, g_logFile);
        std::fputc('\n', g_logFile);
        std::fflush(g_logFile); // (écrit tout de suite : utile si l'appli plante)
    }
    if (g_defaultLog)
        g_defaultLog(userdata, category, priority, message);
}

void startLogFile(const fs::path& dataDir)
{
    std::error_code ec;
    fs::create_directories(dataDir, ec);
#ifdef _WIN32
    if (_wfopen_s(&g_logFile, (dataDir / "NexoRetro.log").c_str(), L"w") != 0)
        g_logFile = nullptr;
#else
    g_logFile = std::fopen((dataDir / "NexoRetro.log").c_str(), "w");
#endif
    g_defaultLog = SDL_GetDefaultLogOutputFunction();
    SDL_SetLogOutputFunction(logToFile, nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    setupConsole(argc, argv);

    AppOptions options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 1;
    }
    if (options.dataDir.empty())
        options.dataDir = defaultDataDir(); // à côté de l'exe, ou %APPDATA%\NexoRetro si installé
    startLogFile(options.dataDir);
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
    if (g_logFile)
        std::fclose(g_logFile);
    g_logFile = nullptr;
    // Une mise à jour à installer ? Tout est fermé et enregistré : on lance
    // l'installateur, qui remplacera le programme puis le relancera.
    AppUpdater::runPendingInstaller();
    return code;
}
