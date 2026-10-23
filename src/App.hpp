#pragma once
// App : le chef d'orchestre. Il possède tous les modules et passe d'un mode
// à l'autre : interface (bibliothèque...) <-> jeu (avec son menu pause).

#include "Audio.hpp"
#include "Bios.hpp"
#include "CoreUpdater.hpp"
#include "Discord.hpp"
#include "FileDialogs.hpp"
#include "FileHasher.hpp"
#include "Remaps.hpp"
#include "Rewind.hpp"
#include "Input.hpp"
#include "Library.hpp"
#include "Settings.hpp"
#include "Ui.hpp"
#include "UserData.hpp"
#include "Video.hpp"
#include "VoiceChat.hpp"
#include "ui/Covers.hpp"
#include "ui/PauseMenu.hpp"
#include "net/LinkPlay.hpp"
#include "net/LinkTunnel.hpp"
#include "net/Netplay.hpp"
#include "net/Session.hpp"
#include "net/Socket.hpp"
#include "scrape/Scraper.hpp"
#include "ui/Screens.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Core;

struct AppOptions {
    std::filesystem::path dataDir; // contient cores/, bios/, roms/, saves/
    std::filesystem::path core;    // si fourni : lancement direct d'un jeu
    std::filesystem::path rom;
    // Mode test : quitter après N images, en écrivant une capture d'écran.
    long long maxFrames = 0;
    std::filesystem::path exitScreenshot;
    // --clavier : n'écouter que le clavier ; --manette N : que la manette N.
    Input::Source inputSource = Input::Source::Auto;
    int inputPad = 0;
    std::string startScreen;       // "library", "game", "bios"... (tests)
    bool openMenu = false;         // ouvrir le menu pause dès le lancement (tests)
    // Test réseau automatique : l'hôte écrit son code dans ce fichier,
    // l'invité le lit et se connecte ; ils échangent un message puis quittent.
    std::filesystem::path netTestHost;
    std::filesystem::path netTestJoin;
    std::string netTestPassword;
    int netTestExpect = 1;     // hôte : nombre d'amis attendus
    bool netTestGame = false;  // hôte : choisir le premier jeu de la bibliothèque
    int netTestPlayFrames = 0; // hôte : lancer une partie en ligne de N images
    int simulatedLatencyMs = 0; // --net-latency : ping simulé (tester sur un seul PC)
    std::string testRemove;     // --test-remove hide|trash : retire le premier jeu (tests)
    std::filesystem::path importBios; // --import-bios <fichier ou dossier> : comme un dépôt sur l'écran BIOS
    std::string installExternal; // --install-external ps3|ps4|xbox|x360 : comme le bouton "Installer"
    std::string importSystemId;  // --import-system <console> <dossier> : comme "Importer…" (fichiers système)
    std::filesystem::path importSystemDir;
    int netTestStart = 0;       // --net-start N : point de départ de l'hôte (0 à 3, voir OnlineState)
    bool netTestLink = false;
    bool testPlay = false;
    bool testResume = false;
    bool openControls = false; // --controls (avec --menu) : ouvre l'écran "Commandes" (tests) // --test-resume : comme "Reprendre" (sauvegarde automatique) sur le premier jeu
    bool bigPicture = false;    // --big-picture : démarre en mode Big Picture (tests)
    int libraryView = -1;       // --library-view 0|1|2 : grille, liste ou carrousel (tests, non enregistré)
    int settingsTab = -1;       // --settings-tab N : onglet des Paramètres à ouvrir (tests)      // --test-play : lance le premier jeu de la bibliothèque (tests)   // --net-link : partie en mode câble link (chacun son jeu)
};

class App {
public:
    explicit App(AppOptions options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

private:
    enum class Mode { Interface, Playing };

    bool init();
    void handleEvent(const SDL_Event& event);
    void drawUi();
    void drawFps();
    void updateNetwork();
    void updateServices(); // chat vocal, mise à jour des cores, Discord
    DiscordPresence::Activity discordActivity() const;
    void updateSalonGame();
    void updateNetplay();
    bool launchNetplayGame(const Game& game);
    void applyPendingState(); // recharge la sauvegarde d'état demandée au lancement
    void updateLink();      // câble link : lancement, relais, fin
    void drawLinkOverlay();
    void runNetplayFrames();
    void drawNetplayOverlay();
    void drawOnlineStats(); // en ligne : qui parle, ping et pertes
    Input::PadState scriptedInput() const;
    void updateNetTest(const net::Session::Event* event);

    // netplay : partie en ligne (sans charger ni écraser la sauvegarde du joueur).
    // keepSave (en ligne, hôte) : partir de sa sauvegarde et l'enregistrer à la fin.
    bool launch(const std::filesystem::path& corePath, const std::filesystem::path& romPath,
                const std::filesystem::path& state, bool netplay = false, bool keepSave = false);
    void closeGame();
    void returnToInterface();
    bool restartGraphics(); // contexte OpenGL "neutre" pour l'interface
    void setWindowIcon();   // le logo de Nexo Retro dans la barre des tâches
    void applySettings();
    void rescan(); // relit la bibliothèque, puis cherche les nouveaux jeux en ligne
    void applyScrapedInfo();

    void setMenuOpen(bool open);
    void handlePauseAction(ui::PauseAction action);
    void drawControlsScreen(); // menu pause > Commandes de la console
    void captureRewind();      // photo de l'image qui vient d'être calculée (retour en arrière)
    void drawTimeBadge();      // "Avance rapide x4" / "Retour en arrière" à l'écran
    void saveState();
    void loadState();
    void takeScreenshot();
    bool launchExternal(const Game& game); // Switch, Xbox, PS3... : émulateur externe
    void updateExternal();                 // compte le temps de jeu quand il se ferme

    std::filesystem::path dir(const char* name) const { return m_options.dataDir / name; }

    AppOptions m_options;

    // [C++] Les membres sont détruits dans l'ordre INVERSE de leur
    // déclaration. Le core (déclaré en dernier) disparaît donc en premier,
    // pendant que la vidéo, l'audio et l'interface dont il dépend existent encore.
    Video m_video;
    Audio m_audio;
    Input m_input;
    Ui m_ui;
    UserData m_userData;
    Settings m_settings;
    Library m_library;
    BiosManager m_bios;
    Covers m_covers;
    FileDialogs m_dialogs;
    ui::Navigation m_nav;
    ui::Shell m_shell;
    ui::PauseMenu m_pauseMenu;
    net::NetworkSystem m_network; // avant m_session : le réseau doit exister plus longtemps
    net::Session m_session;
    net::Netplay m_netplay;
    net::LinkPlay m_link;                       // câble link : chacun sa console
    std::unique_ptr<net::LinkTunnel> m_tunnel;  // câble Game Boy : le relais
    bool m_tunnelAddressesSent = false;
    int m_linkFrames = 0; // images jouées avec le câble branché (tests)
    std::vector<net::Session::Event> m_deferredEvents; // reçus pendant une image (pompe du câble)
    std::map<std::string, std::string> m_launchOptions; // options imposées au prochain lancement
    ui::OnlineState m_online;
    FileHasher m_hasher;
    scrape::Scraper m_scraper;
    ExternalInstaller m_installer; // émulateurs externes installés en un clic
    CoreUpdater m_coreUpdater;     // cores gardés à jour tout seuls
    VoiceChat m_voice;             // chat vocal du salon
    DiscordPresence m_discord;     // "Joue à ..." sur Discord
    std::string m_coreInUse;       // le .dll du jeu en cours (à ne pas mettre à jour pendant la partie)
    Remaps m_remaps;               // commandes propres à chaque console (remaps.txt)
    std::string m_remapKey;        // console (et manette Wii) du jeu en cours
    bool m_controlsOpen = false;   // écran "Commandes" ouvert dans le menu pause
    int m_testMenuFrames = 0;      // --menu : images jouées avant d'ouvrir le menu
    // Retour en arrière et avance rapide.
    Rewind m_rewind;
    bool m_rewindUsable = true; // faux pour les consoles aux états trop gros (PS2, GameCube...)
    enum class TimeMode { Normal, Fast, Rewinding, RewindEnd };
    TimeMode m_timeMode = TimeMode::Normal;
    int m_gameLoops = 0; // tours de boucle passés en jeu (tests du rewind)
    ui::UiContext m_context; // références vers les membres ci-dessus
    // [C++] std::unique_ptr : un pointeur qui possède son objet et le
    // détruit tout seul. Vide quand aucun jeu ne tourne.
    std::unique_ptr<Core> m_core;

    Mode m_mode = Mode::Interface;
    bool m_menuOpen = false;
    bool m_running = true;
    bool m_screenshotNextFrame = false;
    bool m_showImGuiDemo = false;
    std::vector<std::filesystem::path> m_dropped;

    // Le jeu en cours.
    std::filesystem::path m_romPath;
    std::string m_gameTitle;
    std::string m_systemName;
    std::filesystem::path m_gameSaveDir; // saves/<nom du jeu>/
    // Le jeu ouvert dans un émulateur externe (un seul à la fois).
    std::unique_ptr<class ExternalProcess> m_external;
    std::filesystem::path m_externalGame;
    long long m_externalStart = 0;
    Uint64 m_externalChecked = 0;
    Uint64 m_biosChecked = 0; // dernière vérification du dossier bios/
    std::filesystem::path m_sramFile;
    std::filesystem::path m_stateFile;
    std::filesystem::path m_autoStateFile;
    long long m_playStart = 0;
    bool m_netplayGame = false; // le jeu en cours est une partie en ligne
    bool m_netplayKeepsSave = false; // ... partie de la sauvegarde de l'hôte, enregistrée à la fin
    // Sauvegarde d'état à recharger dès que le core a calculé ses premières images.
    std::filesystem::path m_pendingState;
    int m_framesSinceLaunch = 0;
    std::string m_launchError;  // pourquoi le dernier lancement a échoué

    // Images par seconde mesurées (réglage "Afficher les images par seconde").
    Uint64 m_fpsWindowStart = 0;
    int m_fpsFrames = 0;
    float m_fps = 0.0f;

    // Lancer un jeu ou revenir à l'interface détruit le contexte OpenGL.
    // Impossible de le faire au milieu d'une image d'ImGui : on note la
    // demande, et on l'exécute une fois l'image terminée.
    struct LaunchRequest {
        std::filesystem::path core;
        std::filesystem::path rom;
        std::filesystem::path state;
    };
    std::optional<LaunchRequest> m_pendingLaunch;
    bool m_pendingReturn = false;

    // Test réseau automatique (--net-host / --net-join).
    bool m_netTestCodeWritten = false;
    bool m_netTestJoined = false;
    bool m_netTestGamePicked = false;
    bool m_netTestStatusesLogged = false;
    bool m_netTestScript = false; // touches simulées pendant le test
    bool m_netTestStarted = false;
    int m_netTestHellos = 0;
    Uint64 m_netTestQuitAt = 0;
};
