#include "App.hpp"

#include "Core.hpp"
#include "Cheats.hpp"
#include "Lang.hpp"
#include "LibraryTools.hpp"
#include "ExternalProcess.hpp"
#include "ExternalSetup.hpp"
#include "WiiControllers.hpp"
#include "SaveFiles.hpp"
#include "CoreOptions.hpp"
#include "Hash.hpp"
#include "Trash.hpp"
#include "UiSounds.hpp"
#include "Utf8.hpp"
#include "scrape/GameDatabase.hpp"
#include "ui/Draw.hpp"
#include "ui/Theme.hpp"
#include "ui/Themes.hpp"
#include "ui/Widgets.hpp"

#include <imgui.h>
#include <stb_image.h> // (l'implémentation est dans ui/Covers.cpp)

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace {

long long now()
{
    return static_cast<long long>(std::time(nullptr));
}

ui::Screen screenFromName(const std::string& name)
{
    if (name == "library") return ui::Screen::Library;
    if (name == "game") return ui::Screen::Game;
    if (name == "add") return ui::Screen::AddGames;
    if (name == "bios") return ui::Screen::Bios;
    if (name == "friends") return ui::Screen::Friends;
    if (name == "speedrun") return ui::Screen::Speedrun;
    if (name == "settings") return ui::Screen::Settings;
    return ui::Screen::Home;
}

} // namespace

App::App(AppOptions options)
    : m_options(std::move(options)),
      m_userData(dir("userdata.txt")),
      m_library(dir("roms"), dir("cores")),
      m_bios(dir("bios"), fromUtf8(SDL_GetBasePath()) / "assets" / "database" / "System.dat"),
      m_hasher(dir("hashes.txt")),
      m_scraper(dir("scraper")),
      m_installer(dir("emulators")),
      m_coreUpdater(dir("cores")),
      m_appUpdater(fromUtf8(SDL_GetBasePath())), // (installed.txt est à côté de l'exe, pas des données)
      m_remaps(dir("remaps.txt")),
      m_speedrun(dir("speedrun.txt")),
      // Le contexte des écrans : des références vers nos membres, et des
      // lambdas qui appellent nos méthodes. [C++] [this] capture l'objet App.
      m_context{m_library, m_userData, m_settings, m_bios, m_covers, m_input, m_nav, m_session, m_relayServer, m_online, m_scraper, m_installer,
                m_voice, m_coreUpdater, m_discord, m_portableCopy, m_speedrun, m_options.dataDir,
                [this](const Game& game, const fs::path& state) {
                    m_settings.rollParentalDay();
                    if (m_settings.parentalSecondsLeft() == 0) {
                        m_ui.notify("Temps de jeu du jour écoulé : un parent peut en ajouter (Paramètres > Contrôle parental)");
                        return;
                    }
                    // Switch, Xbox, PS3... : un programme à part lance le jeu.
                    if (game.system->external())
                        launchExternal(game);
                    else
                        m_pendingLaunch = LaunchRequest{m_library.corePath(game), game.path, state};
                },
                [this](std::string text) { m_ui.notify(std::move(text)); },
                [this](DialogPurpose purpose) {
                    switch (purpose) {
                    case DialogPurpose::AddGameFiles: m_dialogs.openFiles(m_video.window(), purpose, false, true); break;
                    case DialogPurpose::AddGameFolder: m_dialogs.openFolder(m_video.window(), purpose); break;
                    case DialogPurpose::AddBiosFile: m_dialogs.openFiles(m_video.window(), purpose, false, true); break;
                    case DialogPurpose::AddBiosFolder: m_dialogs.openFolder(m_video.window(), purpose); break;
                    case DialogPurpose::ImportSystemFiles: m_dialogs.openFiles(m_video.window(), purpose, false, true); break;
                    case DialogPurpose::ChooseCover:
                    case DialogPurpose::ChooseBackground: m_dialogs.openFiles(m_video.window(), purpose, true, false); break;
                    case DialogPurpose::ChooseFont: m_dialogs.openFonts(m_video.window(), purpose); break;
                    case DialogPurpose::ImportSave:
                    case DialogPurpose::ChooseExternal: m_dialogs.openFiles(m_video.window(), purpose, false, false); break;
                    case DialogPurpose::ImportBios: m_dialogs.openZip(m_video.window(), purpose); break;
                    case DialogPurpose::MakePortable:
                    case DialogPurpose::ChooseSpeedrunFolder: m_dialogs.openFolder(m_video.window(), purpose); break;
                    case DialogPurpose::ImportSplits:
                        m_dialogs.openFile(m_video.window(), purpose, "Splits LiveSplit", "lss");
                        break;
                    case DialogPurpose::ExportSplits:
                        m_dialogs.saveFile(m_video.window(), purpose, "splits.lss", "Splits LiveSplit", "lss");
                        break;
                    case DialogPurpose::ExportLibraryCsv:
                        m_dialogs.saveFile(m_video.window(), purpose, "NexoRetro-collection.csv", "CSV", "csv");
                        break;
                    case DialogPurpose::ExportLibraryHtml:
                        m_dialogs.saveFile(m_video.window(), purpose, "NexoRetro-collection.html", "HTML", "html");
                        break;
                    case DialogPurpose::ExportBios: {
                        // Nom proposé : NexoRetro-BIOS-2026-10-23.zip
                        const std::time_t now = std::time(nullptr);
                        std::tm day{};
                        localtime_s(&day, &now);
                        char name[64];
                        std::strftime(name, sizeof(name), "NexoRetro-BIOS-%Y-%m-%d.zip", &day);
                        m_dialogs.saveZip(m_video.window(), purpose, name);
                        break;
                    }
                    }
                },
                [this] { rescan(); },
                [this] {
                    m_settings.save(dir("settings.ini"));
                    if (m_settings.autoScrape)
                        m_scraper.requestMissing(m_library.games());
                    applySettings();
                    m_session.setNickname(m_settings.nickname);
                },
                [this](const Game& game) {
                    m_online.picking = game.path;
                    m_online.linkChoice = game.path; // (câble link : l'hôte joue ce jeu-là)
                },
                [this] {
                    std::string error;
                    const auto& game = m_session.game();
                    bool ok = false;
                    if (game && game->link) {
                        // Câble link : GBA par paquets (gpSP), Game Boy par le relais (Gambatte).
                        // Le transport dépend du core : paquets (gpSP, melonDS DS)
                        // ou relais du câble Game Boy (Gambatte).
                        if (const auto* console = net::LinkPlay::consoleLink(game->systemId))
                            ok = m_link.hostStart(m_session, console->transport, error);
                        else
                            error = "Cette console n'a pas de câble link";
                    } else {
                        ok = m_netplay.hostStart(m_session, error);
                    }
                    if (!ok)
                        m_ui.notify(error);
                },
                [this](bool link) {
                    // Le même jeu, annoncé à nouveau avec le mode : chacun revérifie.
                    if (std::optional<net::SalonGame> game = m_session.game(); game && game->link != link) {
                        game->link = link;
                        m_session.selectGame(*game);
                    }
                },
                [this](const Game& game, bool deleteFiles) {
                    // [C++] Copies : "game" pointe dans la liste de la
                    // bibliothèque, qui sera reconstruite par rescan().
                    const fs::path path = game.path;
                    const std::string title = game.title;
                    if (deleteFiles) {
                        std::string error;
                        if (!moveToTrash(Library::filesOf(path), error)) {
                            m_ui.notify("Impossible de supprimer « " + title + " » : " + error);
                            return;
                        }
                        m_scraper.forget(game);
                        m_userData.forget(path); // temps de jeu, favori... (les sauvegardes restent)
                    } else {
                        m_userData.hide(path);
                    }
                    m_userData.save();
                    m_nav.go(ui::Screen::Library);
                    rescan();
                    m_ui.notify(deleteFiles ? "« " + title + " » envoyé à la Corbeille"
                                            : "« " + title + " » retiré de la bibliothèque");
                },
                [this](const fs::path& dll) {
                    // Un core sans jeu : il annonce ses options dès son
                    // initialisation, puis on le referme aussitôt.
                    std::vector<CoreOption> options;
                    if (m_core)
                        return options; // (jamais pendant une partie)
                    Core probe(m_video, m_audio, m_input);
                    probe.setSystemDirectory(toUtf8(dir("bios")));
                    probe.setSaveDirectory(toUtf8(dir("saves")));
                    if (probe.load(dll))
                        options = probe.optionList();
                    // Celles vues pendant un vrai jeu peuvent être plus complètes.
                    std::vector<CoreOption> seen = loadOptionList(dir("core-options") / fromUtf8(toUtf8(dll.stem()) + ".txt"));
                    return seen.size() > options.size() ? seen : options;
                },
                [this](bool on) {
                    // Big Picture : toute la place de l'écran. (Pas pendant les
                    // tests automatiques : la fenêtre de test reste comme elle est.)
                    if (m_nav.bigPicture == on)
                        return;
                    m_nav.bigPicture = on;
                    if (m_options.maxFrames == 0)
                        m_video.setFullscreen(on);
                }}
{
    m_covers.setScraper(&m_scraper);
    m_context.appUpdater = &m_appUpdater;
    m_context.installUpdate = [this] { installAppUpdate(); };
}

void App::rescan()
{
    m_library.scan(m_userData);
    // Un neogeo.zip rangé avec les jeux : on le copie là où le core le
    // cherche aussi (bios/fbneo/), pour que tous les jeux Neo Geo le trouvent.
    for (const fs::path& file : m_library.biosFound()) {
        const BiosSystem* neogeo = m_bios.forSystem("neogeo");
        if (scrape::matchKey(toUtf8(file.stem())) == "neogeo" && neogeo && !neogeo->ready)
            m_ui.notify(m_bios.import(file) + " (trouvé parmi vos jeux)");
    }
    applyScrapedInfo();
    // Les nouveaux jeux sont cherchés en ligne tout de suite (en arrière-plan).
    if (m_settings.autoScrape)
        m_scraper.requestMissing(m_library.games());
}

// Ce que le scraper apprend sur les jeux d'arcade :
// - leur vrai nom : ils s'appellent "sf2.zip", "kof98.zip" (sauf si
//   l'utilisateur a renommé le jeu) ;
// - le bon émulateur : un jeu que seule la base MAME connaît (Tekken 3...)
//   n'existe pas dans FBNeo, il lui faut le core MAME.
void App::applyScrapedInfo()
{
    m_library.refine([this](Game& g) {
        const std::string_view id = g.system->id;
        if (id != "arcade" && id != "neogeo")
            return false;
        const scrape::GameInfo* info = m_scraper.find(g.path);
        if (!info)
            return false;
        bool changed = false;
        const GameRecord* r = m_userData.find(g.path);
        if (!info->name.empty() && !(r && !r->title.empty())) {
            std::string title = scrape::displayName(info->name);
            changed = title != g.title;
            g.title = std::move(title);
        }
        if (info->database == "MAME")
            g.preferredCores = "mame_libretro.dll";
        return changed;
    });
}

// [C++] Le destructeur est défini ici, et non dans App.hpp, car détruire le
// unique_ptr<Core> demande de connaître la classe Core en entier.
App::~App()
{
    closeGame();
    // Les jaquettes sont des textures OpenGL : à libérer avant le contexte.
    m_covers.releaseTextures();
    sounds::shutdown();
}

bool App::init()
{
    std::error_code ec;
    for (const char* name : {"bios", "saves", "cores", "roms", "covers", "themes"})
        fs::create_directories(dir(name), ec);
    m_userData.load();
    // Tout premier démarrage (pas encore de réglages) : l'assistant s'affichera.
    std::error_code firstEc;
    const bool firstRun = !fs::exists(dir("settings.ini"), firstEc);
    m_settings.load(dir("settings.ini"));
    if (firstRun || m_options.welcome)
        m_settings.firstRunDone = false;
    m_speedrun.load();
    // Les thèmes (intégrés + fichiers themes/*.ini) et celui choisi : avant
    // de créer l'interface, pour charger tout de suite la bonne police.
    theme::loadThemes(dir("themes"));
    theme::applySettings(m_settings);
    // Les sons de l'interface, calculés une fois pour toutes.
    sounds::init();
    m_input.load(dir("input.ini"));
    m_input.setSource(m_options.inputSource, m_options.inputPad);
    m_netplay.setSimulatedLatency(m_options.simulatedLatencyMs);
    // Tests : EMU_NET_LOSS=10 (% de paquets perdus), EMU_NET_JITTER=30 (± ms).
    m_netplay.setSimulatedLoss(SDL_getenv("EMU_NET_LOSS") ? SDL_atoi(SDL_getenv("EMU_NET_LOSS")) : 0,
                               SDL_getenv("EMU_NET_JITTER") ? SDL_atoi(SDL_getenv("EMU_NET_JITTER")) : 0);
    m_online.startFrom = m_options.netTestStart;
    m_input.openConnected();
    m_session.setNickname(m_settings.nickname);
    m_bios.refresh();
    // Les cores installés : une nouvelle version ? (au plus une fois par jour,
    // en arrière-plan ; pas pendant les tests automatiques, sauf EMU_TEST_CORE_UPDATE).
    const bool automatedTest = m_options.maxFrames > 0 || !m_options.netTestHost.empty() || !m_options.netTestJoin.empty();
    m_voice.setMicAllowed(!automatedTest);
    if (m_settings.coreAutoUpdate && (!automatedTest || SDL_getenv("EMU_TEST_CORE_UPDATE")))
        m_coreUpdater.check(SDL_getenv("EMU_TEST_CORE_UPDATE") != nullptr);
    // Nexo Retro lui-même : une nouvelle version ? Même règle (au plus une
    // fois par jour, jamais pendant les tests automatiques). Les tests la
    // forcent avec EMU_TEST_APP_UPDATE, et EMU_UPDATE_URL désigne alors un
    // faux serveur local (voir AppUpdater.cpp).
    if (const char* test = SDL_getenv("EMU_TEST_APP_UPDATE"))
        m_testAppUpdate = test;
    if (!m_testAppUpdate.empty())
        m_appUpdater.check(true);
    else if (m_settings.appAutoCheck && !automatedTest && now() - m_settings.appLastCheck >= 24 * 3600)
        m_appUpdater.check(true);

    // Taille de la fenêtre : celle des maquettes (1440 x 900), agrandie selon
    // l'échelle de l'écran, sans dépasser 90 % de la place disponible.
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    float scale = SDL_GetDisplayContentScale(display);
    if (scale <= 0.0f)
        scale = 1.0f;
    SDL_Rect usable{0, 0, 1920, 1080};
    SDL_GetDisplayUsableBounds(display, &usable);
    const int width = std::min(static_cast<int>(1440 * scale), static_cast<int>(usable.w * 0.9f));
    const int height = std::min(static_cast<int>(900 * scale), static_cast<int>(usable.h * 0.9f));

    const bool directLaunch = !m_options.core.empty();
    if (!m_video.open("Nexo Retro", width, height, directLaunch))
        return false;
    setWindowIcon();
    // Le pseudo dans le titre : pratique pour distinguer deux fenêtres.
    const std::string title = "Nexo Retro · " + m_settings.nickname;
    m_video.setTitle(title.c_str());
    const fs::path fontDir = fromUtf8(SDL_GetBasePath()) / "assets" / "fonts";
    if (!m_video.createContext({}) || !m_ui.init(m_video.window(), fontDir) || !m_ui.initRenderer())
        return false;
    applySettings();

    rescan();
    m_nav.screen = screenFromName(m_options.startScreen);
    if (m_options.startScreen == "pad1") {
        m_nav.screen = ui::Screen::Settings;
        m_nav.settingsTab = 2;
        m_nav.settingsDevice = 0;
    }
    if (m_options.startScreen == "pads") {
        m_nav.screen = ui::Screen::Settings;
        m_nav.settingsTab = 2;
    }
    if (m_options.startScreen == "infos") {
        m_nav.screen = ui::Screen::Settings;
        m_nav.settingsTab = 6;
    }
    if (m_options.startScreen == "options") {
        m_nav.screen = ui::Screen::Settings;
        m_nav.settingsTab = 7;
    }
    if (m_options.startScreen == "save" || m_options.startScreen == "cheats") {
        m_nav.screen = ui::Screen::Game;
        (m_options.startScreen == "save" ? m_nav.openSave : m_nav.openCheats) = true;
    }
    if (m_options.startScreen == "delete") {
        m_nav.screen = ui::Screen::Game;
        m_nav.openDelete = true;
    }
    if (!m_options.testRemove.empty() && !m_library.games().empty()) {
        const Game& first = m_library.games().front();
        SDL_Log("TEST : retrait de « %s » (%s)", first.title.c_str(), m_options.testRemove.c_str());
        m_context.removeGame(first, m_options.testRemove == "trash");
    }
    if (!m_options.importBios.empty()) {
        std::error_code biosEc;
        const fs::path& source = m_options.importBios;
        const std::string result = fs::is_directory(source, biosEc) ? m_bios.importFolder(source)
                                   : toUtf8(source.extension()) == ".zip" ? m_bios.importArchive(source)
                                                                          : m_bios.import(source);
        SDL_Log("TEST BIOS : %s", result.c_str());
        m_ui.notify(result);
    }
    if (!m_options.testLss.empty()) {
        speedrun::Category cat;
        std::string game;
        std::string error;
        if (speedrun::importLss(m_options.testLss, cat, game, error)) {
            SDL_Log("TEST LSS : %s / %s, %zu splits, %d essais, PB %s, SoB %s", game.c_str(), cat.name.c_str(), cat.segments.size(),
                    cat.attempts, speedrun::formatTime(cat.pb()).c_str(), speedrun::formatTime(cat.sumOfBest()).c_str());
            fs::path out = m_options.testLss;
            out.replace_extension(".export.lss");
            speedrun::Category again;
            std::string game2;
            const bool round = speedrun::exportLss(out, cat, game) && speedrun::importLss(out, again, game2, error);
            SDL_Log("TEST LSS : aller-retour %s (PB %s, SoB %s)", round ? "OK" : error.c_str(), speedrun::formatTime(again.pb()).c_str(),
                    speedrun::formatTime(again.sumOfBest()).c_str());
        } else {
            SDL_Log("TEST LSS : %s", error.c_str());
        }
    }
    if (!m_options.testSrcom.empty())
        m_testSrcom.search(m_options.testSrcom);
    if (!m_options.exportLibrary.empty()) {
        const fs::path& file = m_options.exportLibrary;
        const std::string result = toUtf8(file.extension()) == ".csv"
                                       ? library_tools::exportCsv(m_library.games(), m_userData, m_scraper, file)
                                       : library_tools::exportHtml(m_library.games(), m_userData, m_scraper, file);
        SDL_Log("TEST BIBLIOTHÈQUE : %s", result.c_str());
    }
    m_nav.openDuplicates = m_options.testDuplicates;
    if (m_options.welcome)
        m_nav.welcomeStep = m_options.welcomeStep;
    if (m_options.testDuplicates)
        for (const library_tools::Duplicates& d : library_tools::findDuplicates(m_library.games())) {
            std::string names;
            for (const Game* g : d.games)
                names += " | " + toUtf8(g->path.filename());
            SDL_Log("TEST DOUBLONS : %s%s", d.identical ? "identiques" : "versions", names.c_str());
        }
    if (!m_options.portableCopy.empty())
        ui::startPortableCopy(m_context, m_options.portableCopy);
    if (!m_options.drop.empty())
        m_shell.onDrop(m_context, m_options.drop);
    if (!m_options.exportBios.empty()) {
        const std::string result = m_bios.exportTo(m_options.exportBios);
        SDL_Log("TEST BIOS : %s", result.c_str());
        m_ui.notify(result);
    }
    if (m_options.installExternal.starts_with("core:")) // --install-external core:pcsx2_libretro.dll
        m_installer.startCore(m_options.installExternal.substr(5), dir("cores"));
    else if (m_options.installExternal.starts_with("files:")) // --install-external files:Dolphin
        m_installer.startSystemFiles(m_options.installExternal.substr(6), dir("bios"));
    else if (!m_options.installExternal.empty())
        m_installer.start(m_options.installExternal);
    if (const System* system = Library::systemById(m_options.importSystemId)) {
        std::vector<fs::path> files;
        std::error_code dirEc;
        for (auto it = fs::directory_iterator(m_options.importSystemDir, dirEc); !dirEc && it != fs::directory_iterator(); it.increment(dirEc))
            files.push_back(it->path());
        SDL_Log("TEST SYSTÈME : %s",
                external::importSystemFiles(system->id, m_library.externalPath(*system), dir("bios"), files).c_str());
    }
    if (m_options.bigPicture || m_settings.bigPictureOnStart)
        m_context.setBigPicture(true);
    if (m_options.libraryView >= 0)
        m_settings.libraryView = m_options.libraryView;
    if (m_options.settingsTab >= 0)
        m_nav.settingsTab = m_options.settingsTab;
    if (m_options.testPlay && !m_library.games().empty())
        m_context.play(m_library.games().front(), {});
    if (m_options.testResume && !m_library.games().empty())
        m_context.play(m_library.games().front(), ui::autoStateFile(m_context, m_library.games().front()));
    if (m_options.startScreen == "views") { // le menu « Vue » de la bibliothèque (tests)
        m_nav.screen = ui::Screen::Library;
        m_nav.openViews = true;
    }
    if (m_options.startScreen == "filters") {
        m_nav.screen = ui::Screen::Library;
        m_nav.openFilters = true;
    }
    if (m_options.startScreen == "covers") {
        m_nav.screen = ui::Screen::Game;
        m_nav.openCoverPicker = true;
    }
    if (m_nav.screen == ui::Screen::Game) {
        if (m_library.games().empty())
            m_nav.screen = ui::Screen::Library;
        else
            m_nav.game = m_library.games().front().path;
    }
    if (directLaunch && !launch(m_options.core, m_options.rom, {}))
        return false;
    // --menu : le menu pause s'ouvre après une seconde de jeu (voir run()),
    // le temps que le core ait décrit ses boutons.
    return true;
}

int App::run()
{
    if (!init())
        return 1;

    Uint64 nextFrame = SDL_GetTicksNS();
    long long frameCount = 0;
    m_fpsWindowStart = SDL_GetTicks();
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            handleEvent(event);
        for (const DialogResult& result : m_dialogs.poll())
            m_shell.onDialogResult(m_context, result);
        updateNetwork();
        updateServices();
        // Où est l'image du jeu : la souris y sert de pointeur (Wiimote).
        m_input.setGameRect(m_video.gameRect()[0], m_video.gameRect()[1], m_video.gameRect()[2], m_video.gameRect()[3]);

        // En ligne, le jeu ne se met jamais en pause (les autres jouent).
        const bool online = m_mode == Mode::Playing && m_netplay.active();
        const bool emulating = m_mode == Mode::Playing && (!m_menuOpen || online);
        if (online) {
            runNetplayFrames();
        } else if (emulating) {
            // Avance rapide (Tab, ou Select + R) et retour en arrière (Retour
            // arrière, ou Select + L). Pas en ligne ni avec le câble : les
            // autres joueurs, eux, avancent à vitesse normale.
            const bool solo = !m_link.active() && !m_netplay.active() && !m_menuOpen;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            // Tests : EMU_TEST_REWIND / EMU_TEST_FAST = "début,durée" (en tours de
            // boucle de jeu) simulent le raccourci tenu pendant ce temps.
            auto testHeld = [this](const char* var) {
                const char* v = SDL_getenv(var);
                if (!v)
                    return false;
                const int start = SDL_atoi(v);
                const char* comma = SDL_strchr(v, ',');
                const int length = comma ? SDL_atoi(comma + 1) : 0;
                return m_gameLoops >= start && m_gameLoops < start + length;
            };
            ++m_gameLoops;
            const bool wantRewind = solo && m_settings.rewind &&
                                    (keys[SDL_SCANCODE_BACKSPACE] || testHeld("EMU_TEST_REWIND") ||
                                     m_input.padChordHeld(SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
            const bool wantFast = solo && (keys[SDL_SCANCODE_TAB] || testHeld("EMU_TEST_FAST") ||
                                           m_input.padChordHeld(SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
            const TimeMode previousMode = m_timeMode;
            m_timeMode = TimeMode::Normal;
            if (wantRewind && m_rewindUsable) {
                // Début du retour en arrière : le point de départ de la frise.
                if (previousMode != TimeMode::Rewinding && previousMode != TimeMode::RewindEnd)
                    m_rewindFrom = m_rewind.newest();
                // Une image en arrière : la photo précédente, puis une image
                // calculée sans le son, pour l'afficher.
                std::string previous;
                if (m_rewind.pop(previous) && m_core->unserialize(previous)) {
                    m_audio.setMuted(true);
                    m_core->runFrame();
                    m_audio.setMuted(false);
                    m_audio.clear();
                    m_timeMode = TimeMode::Rewinding;
                    m_speedrun.markAssisted();
                } else {
                    m_timeMode = TimeMode::RewindEnd; // tout le passé gardé a été remonté
                }
                ++m_fpsFrames;
            } else {
                const int steps = wantFast ? std::clamp(m_settings.fastForwardSpeed, 2, 8) : 1;
                if (wantFast) {
                    m_timeMode = TimeMode::Fast;
                    m_speedrun.markAssisted();
                    m_audio.setMuted(true); // un son accéléré serait inaudible
                }
                for (int step = 0; step < steps; ++step) {
                    // Câble link : les paquets arrivés vont au core avant l'image,
                    // ceux qu'il a produits partent après.
                    const bool cable = m_link.running() && m_link.transport() == net::LinkPlay::Transport::Netpacket;
                    if (cable) {
                        for (net::LinkPlay::Received& r : m_link.takeReceived())
                            m_core->netpacketReceive(r.data, r.from);
                        m_core->netpacketPoll();
                    }
                    m_core->runFrame();
                    // Reprise d'une partie : l'état est rechargé après quelques images
                    // (le temps que le core ait vraiment démarré sa machine).
                    if (!m_pendingState.empty() && ++m_framesSinceLaunch >= 3)
                        applyPendingState();
                    // Tests : --menu (et --controls) ouvrent le menu après 60 images.
                    if (m_options.openMenu && ++m_testMenuFrames == 60) {
                        setMenuOpen(true);
                        m_controlsOpen = m_options.openControls;
                        m_slotsOpen = m_options.openSlots;
                    }
                    // --slots : deux emplacements remplis avant d'ouvrir le menu.
                    if (m_options.openSlots && (m_testMenuFrames == 20 || m_testMenuFrames == 45))
                        m_pendingSlot = m_testMenuFrames == 20 ? 0 : 3;
                    if (m_link.running())
                        ++m_linkFrames;
                    if (cable)
                        for (const Core::NetPacket& p : m_core->takeNetpackets())
                            m_link.send(m_session, p.to, p.data);
                    // Si trop de son s'accumule (avance rapide, ralentissement...), on
                    // vide la file pour ne pas entendre le jeu avec du retard.
                    if (m_audio.queuedSeconds() > m_settings.maxAudioQueue())
                        m_audio.clear();
                    ++m_fpsFrames;
                    ++m_emuFrames;
                    captureRewind();
                }
                if (wantFast) {
                    m_audio.setMuted(false);
                    m_audio.clear();
                }
            }
        }
        updateExternal();
        // Le dossier bios/ modifié à la main (Explorateur) : l'écran BIOS se
        // met à jour tout seul. Une fois par seconde, et pas pendant le jeu.
        if (m_mode != Mode::Playing && SDL_GetTicks() - m_biosChecked > 1000) {
            m_biosChecked = SDL_GetTicks();
            if (m_bios.refreshIfChanged())
                SDL_Log("Dossier des BIOS modifié : écran mis à jour");
        }
        const Uint64 ticks = SDL_GetTicks();
        if (ticks - m_fpsWindowStart >= 500) {
            m_fps = m_fpsFrames * 1000.0f / static_cast<float>(ticks - m_fpsWindowStart);
            m_fpsFrames = 0;
            m_fpsWindowStart = ticks;
        }

        if (m_screenshotNextFrame) {
            takeScreenshot();
            m_screenshotNextFrame = false;
        }

        m_video.renderGame();
        updateSpeedrun();
        updateParental();
        // L'image de la vidéo : celle du jeu seul, ou (réglage) avec ce qui est
        // dessiné par-dessus, chrono et touches compris (les notifications
        // sont alors cachées pendant l'enregistrement).
        const bool overlayInVideo = m_settings.speedrunVideoOverlay;
        auto recordFrame = [&] {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> pixels;
            if (m_video.readGameArea(pixels, w, h))
                m_recorder.addFrame(std::move(pixels), w, h, m_emuFrames);
        };
        if (m_recorder.recording() && !overlayInVideo)
            recordFrame();
        m_ui.setNotificationsHidden(m_recorder.recording() && overlayInVideo);
        // Emplacements de sauvegarde : la miniature est prise sur l'image du jeu seul.
        if (m_pendingSlot >= 0)
            saveSlot(m_pendingSlot);
        captureRewindThumbnail();
        drawUi();
        if (m_recorder.recording() && overlayInVideo)
            recordFrame();

        ++frameCount;
        if (m_options.maxFrames > 0 && frameCount >= m_options.maxFrames) {
            if (!m_options.exitScreenshot.empty())
                m_video.requestScreenshot(m_options.exitScreenshot);
            m_running = false;
        }
        m_video.swap();

        // L'image est finie : on peut maintenant changer de contexte OpenGL.
        if (m_pendingLaunch) {
            const LaunchRequest request = std::move(*m_pendingLaunch);
            m_pendingLaunch.reset();
            launch(request.core, request.rom, request.state);
            nextFrame = SDL_GetTicksNS();
        }
        if (m_pendingReturn) {
            m_pendingReturn = false;
            returnToInterface();
        }

        // Cadence : on vise exactement la fréquence d'images de la console
        // (ou 60 images/s dans les menus). nextFrame est une date absolue :
        // les petites erreurs de sommeil ne s'accumulent pas.
        const Uint64 nowNs = SDL_GetTicksNS();
        // (Le jeu a pu se fermer pendant cette image : on revérifie m_core.)
        if (emulating && !online && m_core && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB]) {
            nextFrame = nowNs; // avance rapide : aucune attente (jamais en ligne)
            continue;
        }
        const double fps = (emulating && m_core && m_core->avInfo().timing.fps > 0.0) ? m_core->avInfo().timing.fps : 60.0;
        nextFrame += static_cast<Uint64>(1e9 / fps);
        if (nextFrame > nowNs)
            SDL_DelayPrecise(nextFrame - nowNs);
        else if (nowNs - nextFrame > 100'000'000) // plus de 100 ms de retard (fenêtre déplacée...)
            nextFrame = nowNs;
    }
    // "Installer automatiquement les mises à jour" : une version téléchargée
    // et vérifiée s'installe maintenant, à la fermeture (l'installateur part
    // à la toute fin du programme, une fois tout enregistré : voir main.cpp).
    if (m_settings.appAutoInstall && !m_appUpdater.installScheduled() &&
        m_appUpdater.state() == AppUpdater::State::Ready &&
        m_appUpdater.latestVersion() != m_settings.appIgnoredVersion)
        m_appUpdater.scheduleInstall();
    return 0;
}

void App::handleEvent(const SDL_Event& event)
{
    m_ui.processEvent(event);
    // Branchements de manettes, et capture d'une touche à réassigner.
    m_input.handleEvent(event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
        m_running = false;
        break;

    case SDL_EVENT_DROP_FILE:
        if (event.drop.data)
            m_dropped.push_back(fromUtf8(event.drop.data));
        break;
    case SDL_EVENT_DROP_COMPLETE:
        if (m_mode == Mode::Interface && !m_dropped.empty())
            m_shell.onDrop(m_context, m_dropped);
        m_dropped.clear();
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        // Raccourci manette du menu pause, selon le réglage choisi.
        if (m_mode == Mode::Playing) {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(event.gbutton.which);
            const bool combo = event.gbutton.button == SDL_GAMEPAD_BUTTON_START && pad &&
                               SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
            const bool guide = event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE;
            // Le chrono des speedruns à la manette (boutons choisis dans l'écran Speedrun).
            if (!m_menuOpen && event.gbutton.button == m_settings.speedrunPadSplit)
                speedrunKey(SDLK_F5);
            if (!m_menuOpen && event.gbutton.button == m_settings.speedrunPadReset)
                speedrunKey(SDLK_F9);
            if ((m_settings.menuButton == 0 && combo) || (m_settings.menuButton == 1 && guide))
                setMenuOpen(!m_menuOpen);
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat)
            break;
        if (event.key.key == SDLK_F11) {
            m_video.toggleFullscreen();
        } else if (m_mode == Mode::Interface) {
            if (event.key.key == SDLK_F5) {
                rescan();
                m_bios.refresh();
            } else if (event.key.key == SDLK_F1) {
                m_showImGuiDemo = !m_showImGuiDemo;
            }
        } else {
            switch (event.key.key) {
            case SDLK_ESCAPE: setMenuOpen(!m_menuOpen); break;
            case SDLK_F2:
            case SDLK_F4:
            case SDLK_F8:
                if (m_netplay.active())
                    m_ui.notify("Indisponible pendant une partie en ligne");
                else if (event.key.key == SDLK_F2)
                    saveState();
                else if (event.key.key == SDLK_F4)
                    loadState();
                else
                    m_core->reset();
                break;
            case SDLK_F12: takeScreenshot(); break;
            case SDLK_F5:
            case SDLK_F6:
            case SDLK_F7:
            case SDLK_F9: speedrunKey(event.key.key); break;
            default: break;
            }
        }
        break;

    default:
        break;
    }
}

void App::drawUi()
{
    // Pas de sons d'interface pendant un jeu (même dans le menu pause).
    sounds::setSuspended(m_mode == Mode::Playing);
    m_ui.beginFrame();
    m_covers.newFrame();
    // Les résultats du scraper (jaquettes, infos) arrivent ici.
    if (m_scraper.update())
        applyScrapedInfo();
    for (std::string& message : m_scraper.takeMessages())
        m_ui.notify(std::move(message));

    if (m_mode == Mode::Interface) {
        m_shell.draw(m_context);
        // F1 : la démo officielle d'ImGui, pour découvrir tous ses widgets.
        if (m_showImGuiDemo)
            ImGui::ShowDemoWindow(&m_showImGuiDemo);
    } else {
        if (m_settings.showFps)
            drawFps();
        drawTimeBadge();
        if (m_speedrun.enabled())
            drawSpeedrunOverlay();
        // (Pas par-dessus le menu ni la frise du retour en arrière.)
        if (m_settings.inputDisplay && !m_menuOpen && m_timeMode != TimeMode::Rewinding && m_timeMode != TimeMode::RewindEnd)
            drawInputDisplay();
        if (m_netplay.active())
            drawNetplayOverlay();
        if (m_link.active())
            drawLinkOverlay();
        if (m_netplay.active() || m_link.active())
            drawOnlineStats();
        if (m_timeMode == TimeMode::Rewinding || m_timeMode == TimeMode::RewindEnd)
            drawRewindTimeline();
        if (m_menuOpen && m_controlsOpen) {
            drawControlsScreen();
        } else if (m_menuOpen && m_slotsOpen) {
            drawSlotsScreen();
        } else if (m_menuOpen)
        {
            bool volumeChanged = false;
                const ui::OnlineMenu online{m_netplay.active() || m_link.active(), m_settings.voiceMode != 0,
                                        m_voice.selfMuted(), m_settings.netStats};
            handlePauseAction(m_pauseMenu.draw(m_gameTitle, m_systemName, m_settings.volume, volumeChanged, online));
            if (volumeChanged) {
                m_audio.setVolume(m_settings.volume);
                m_settings.save(dir("settings.ini"));
            }
        }
    }

    m_ui.drawNotifications();
    m_ui.endFrame();
}

void App::updateNetwork()
{
    if (std::optional<SpeedrunCom::Result> r = m_testSrcom.take()) {
        SDL_Log("TEST SRCOM : %s (%s) %s", r->gameName.c_str(), r->gameLink.c_str(), r->error.c_str());
        for (const SpeedrunCom::OfficialCategory& c : r->categories)
            SDL_Log("TEST SRCOM : - %s : WR %s par %s", c.name.c_str(), speedrun::formatTime(c.wrMs).c_str(), c.wrBy.c_str());
    }
    m_session.update();
    m_relayServer.update();
    // Test : un relais qui écrit son code dans un fichier dès qu'il est prêt.
    if (!m_options.netTestRelay.empty()) {
        if (!m_relayServer.running() && !m_netTestRelayWritten)
            m_relayServer.start();
        if (m_relayServer.ready() && !m_netTestRelayWritten) {
            std::ofstream(m_options.netTestRelay) << m_relayServer.code();
            m_netTestRelayWritten = true;
            SDL_Log("TEST RELAIS : code écrit (%s)", m_relayServer.code().c_str());
        }
    }
    // (Ceux mis de côté pendant une image par la pompe du câble, puis les nouveaux.)
    std::vector<net::Session::Event> events = std::move(m_deferredEvents);
    m_deferredEvents.clear();
    for (net::Session::Event& e : m_session.takeEvents())
        events.push_back(std::move(e));
    for (const net::Session::Event& e : events) {
        using Type = net::Session::Event::Type;
        switch (e.type) {
        case Type::Chat: m_online.chat.push_back({e.author, e.text, false}); break;
        case Type::Joined:
        case Type::MemberJoined:
        case Type::GameChanged:
            m_online.chat.push_back({"", e.text, false});
            m_ui.notify(e.text);
            break;
        case Type::MemberLeft:
            m_online.chat.push_back({"", e.text, false});
            m_ui.notify(e.text);
            m_netplay.onMemberLeft(m_session, e.text); // la partie s'arrête pour tous
            m_link.onMemberLeft(m_session, e.text);
            break;
        case Type::Closed:
            m_online.chat.push_back({"", e.text, false});
            m_online.match.clear();
            m_online.picking.clear();
            m_ui.notify(e.text);
            m_netplay.onSalonClosed(e.text);
            m_link.onSalonClosed(e.text);
            break;
        case Type::Info:
        case Type::Error: m_ui.notify(e.text); break;
        case Type::AppMessage:
            // Les messages du câble link commencent par une minuscule.
            if (net::LinkPlay::isLinkMessage(e.data))
                m_link.onAppMessage(m_session, e.from, e.data);
            else
                m_netplay.onAppMessage(m_session, e.from, e.data);
            break;
        }
        updateNetTest(&e);
    }
    updateSalonGame();
    m_netplay.update(m_session);
    updateNetplay();
    m_link.update(m_session);
    updateLink();
    updateNetTest(nullptr);
}

// Démarre le jeu d'une partie en ligne. L'hôte peut partir de sa sauvegarde
// (choix "Point de départ" du salon) ; les autres partent toujours de zéro,
// puisqu'ils reçoivent ensuite l'état de l'hôte.
bool App::launchNetplayGame(const Game& game)
{
    const int start = m_session.isHost() ? m_online.startFrom : 0;
    const std::string stem = toUtf8(game.path.stem());
    const SaveFiles files = saveFilesFor(dir("saves"), stem);
    fs::path state;
    if (start == 2)
        state = files.autoState;
    else if (start == 3)
        state = files.state;
    if (start > 0) {
        static const char* const names[] = {"", "ma progression", "ma sauvegarde automatique", "ma sauvegarde rapide (F2)"};
        m_session.sendChat(std::string("La partie démarre depuis ") + names[start] + " : elle sera enregistrée chez moi.");
    }
    return launch(m_library.corePath(game), game.path, state, true, start > 0);
}

void App::applyPendingState()
{
    if (m_pendingState.empty() || !m_core)
        return;
    m_ui.notify(m_core->loadState(m_pendingState) ? "Partie reprise" : "Impossible de charger la sauvegarde d'état");
    m_pendingState.clear();
}

void App::updateNetplay()
{
    // 1. Démarrer le jeu quand l'hôte lance la partie.
    if (m_netplay.wantsLaunch()) {
        const Game* game = m_online.match.empty() ? nullptr : m_library.find(m_online.match);
        if (!game) {
            m_netplay.launched(m_session, false, "", false, "le fichier du jeu est introuvable");
        } else if (!m_library.hasCore(*game)) {
            m_netplay.launched(m_session, false, "", false, m_library.coreFileFor(*game) + " est manquant");
        } else if (!launchNetplayGame(*game)) {
            m_netplay.launched(m_session, false, "", false,
                               m_launchError.empty() ? std::string("le core n'a pas démarré") : m_launchError);
        } else {
            // Une première image, manette au repos, pour que le core soit
            // complètement initialisé avant de photographier ou recharger son état.
            m_input.setOverride({});
            m_audio.setMuted(true);
            m_core->runFrame();
            // Certains cores (MAME...) ne savent photographier leur état
            // qu'après quelques images de démarrage : on patiente jusqu'à 3 s.
            int frames = 1;
            for (; !m_core->canSerialize() && frames < 180; ++frames)
                m_core->runFrame();
            // L'hôte qui part de sa sauvegarde d'état : rechargée maintenant
            // que le core tourne (avant, PCSX2 plantait), puis envoyée à tous.
            if (!m_pendingState.empty()) {
                for (; frames < 3; ++frames)
                    m_core->runFrame();
                applyPendingState();
            }
            m_audio.setMuted(false);
            if (!m_core->canSerialize()) {
                SDL_Log("Toujours aucune sauvegarde d'état après %d images", frames);
                m_netplay.launched(m_session, false, "", false,
                                   std::string(m_core->info().library_name) + " ne sait pas sauvegarder l'état de « " +
                                       game->title + " » : jeu en ligne impossible avec ce core");
            } else {
                if (frames > 1)
                    SDL_Log("Sauvegarde d'état disponible après %d images", frames);
                // Les options modifiées font partie de "l'émulateur utilisé" :
                // deux joueurs avec des options différentes divergeraient.
                const std::string options = m_core->optionsSignature();
                m_netplay.launched(m_session, true, m_core->coreInfo() + (options.empty() ? "" : " · " + options),
                                   m_core->rollbackSafe(), "");
            }
        }
    }

    // 2. Partir tous du même état : l'hôte l'envoie, les membres le chargent.
    if (m_netplay.hostNeedsState() && m_core) {
        std::string state;
        if (m_core->serialize(state))
            m_netplay.hostSendState(m_session, state);
        else
            m_netplay.stop(m_session, "Ce core ne sait pas faire de sauvegarde d'état : jeu en ligne impossible");
    }
    if (m_netplay.phase() == net::Netplay::Phase::Syncing && m_core) {
        if (const std::optional<std::string> state = m_netplay.takeReceivedState()) {
            if (m_core->unserialize(*state))
                m_netplay.stateApplied(m_session);
            else
                m_netplay.stop(m_session, "Impossible de charger l'état envoyé par l'hôte");
        }
    }

    // 3. Messages et fin de partie.
    for (const std::string& message : m_netplay.takeMessages()) {
        m_online.chat.push_back({"", message, false});
        m_ui.notify(message);
    }
    if (m_netplay.takeStopped()) {
        m_input.clearOverride();
        if (m_mode == Mode::Playing) {
            m_pendingReturn = true;
            m_nav.go(ui::Screen::Friends); // retour au salon
        }
        // Soirée jeu en cours : le jeu suivant est choisi pour tout le monde.
        if (m_session.isHost() && m_online.nightIndex >= 0)
            ui::advanceGameNight(m_context);
    }
}

void App::runNetplayFrames()
{
    if (!m_netplay.running() || !m_core)
        return; // chargement ou synchronisation en cours

    // Mes touches de maintenant (rien si le menu est ouvert).
    net::PadInput local;
    if (!m_menuOpen) {
        const Input::PadState pad = m_netTestScript ? scriptedInput() : m_input.sampleLocal();
        local.buttons = pad.buttons;
        local.analog[0] = pad.analog[0][0];
        local.analog[1] = pad.analog[0][1];
        local.analog[2] = pad.analog[1][0];
        local.analog[3] = pad.analog[1][1];
    }

    // Ce que le netplay peut demander à l'émulateur : photographier son
    // état, le recharger, et calculer une image avec des touches imposées.
    net::Emulator emulator{
        [this](std::string& out) { return m_core->serialize(out); },
        [this](const std::string& state) { return m_core->unserialize(state); },
        [this](const net::FrameInputs& inputs, bool silent) {
            std::array<Input::PadState, 4> ports{};
            for (size_t p = 0; p < ports.size(); ++p) {
                ports[p].buttons = inputs[p].buttons;
                ports[p].analog[0][0] = inputs[p].analog[0];
                ports[p].analog[0][1] = inputs[p].analog[1];
                ports[p].analog[1][0] = inputs[p].analog[2];
                ports[p].analog[1][1] = inputs[p].analog[3];
            }
            m_input.setOverride(ports);
            m_audio.setMuted(silent);
            m_core->setSkipAudioVideo(silent);
            m_core->runFrame();
            m_core->setSkipAudioVideo(false);
            m_audio.setMuted(false);
        },
        [this] { return m_core->memoryFingerprint(); }};
    m_fpsFrames += m_netplay.tick(m_session, emulator, local);

    if (m_audio.queuedSeconds() > m_settings.maxAudioQueue())
        m_audio.clear();
}

Input::PadState App::scriptedInput() const
{
    // Test automatique : chaque joueur "appuie" sur des touches prévisibles,
    // différentes d'un joueur à l'autre, selon le numéro de l'image.
    Input::PadState pad;
    const uint32_t f = m_netplay.frame();
    const uint32_t port = static_cast<uint32_t>(m_netplay.localPort());
    if (f >= 30 && f < 40)
        pad.buttons = 1u << 3; // Start
    else if (f >= 60) {
        pad.buttons = 1u << (4 + (f / 20 + port * 3) % 4); // croix, direction changeante
        if ((f + port * 7) % 40 < 4)
            pad.buttons |= 1u << 0; // B : rejoindre, poser une bombe...
        if ((f + port * 11) % 90 < 4)
            pad.buttons |= 1u << 8; // A
        // Bornes d'arcade : une pièce (Select) puis Start régulièrement, et
        // les autres boutons, pour arriver jusqu'aux combats.
        const uint32_t cycle = (f + port * 37) % 400;
        if (cycle < 6)
            pad.buttons = 1u << 2;
        else if (cycle >= 40 && cycle < 46)
            pad.buttons = 1u << 3;
        else if ((f + port * 5) % 23 < 3)
            pad.buttons |= 1u << (f / 23 % 2 ? 9 : 1) | 1u << (f / 46 % 2 ? 10 : 11); // X/Y, L/R
    }
    return pad;
}

void App::updateSalonGame()
{
    using net::GameStatus;
    m_online.hashProgress = m_hasher.progress();

    // Hôte : le jeu choisi est annoncé dès que son empreinte est calculée.
    if (!m_online.picking.empty()) {
        const Game* game = m_library.find(m_online.picking);
        if (!game || !m_session.isHost() || !m_session.inSalon()) {
            m_online.picking.clear();
        } else if (const std::optional<uint64_t> hash = m_hasher.request(game->path)) {
            // Console portable (Game Boy, GBA, DS) : chacun avait SA console,
            // le salon part donc d'office sur "chacun sa console". L'hôte peut
            // revenir à "même console" (pour jouer ensemble à un jeu solo).
            const bool link = net::LinkPlay::consoleLink(game->system->id) != nullptr;
            m_session.selectGame({*hash, game->size, game->system->id, game->title, link});
            m_online.picking.clear();
        }
    }

    // Tout le monde : a-t-on ce jeu, dans la même version ?
    const std::optional<net::SalonGame>& wanted = m_session.game();
    if (!wanted || !m_session.inSalon()) {
        m_online.match.clear();
        return;
    }
    // Câble link : chacun joue SON jeu de la même console. Prêt dès qu'on en a
    // choisi un (par défaut, celui qui porte le même nom que le jeu du salon).
    if (wanted->link) {
        const Game* chosen = m_online.linkChoice.empty() ? nullptr : m_library.find(m_online.linkChoice);
        if (chosen && chosen->system->id != wanted->systemId)
            chosen = nullptr;
        const Game* only = nullptr; // le seul jeu de cette console, s'il n'y en a qu'un
        int count = 0;
        for (const Game& g : m_library.games()) {
            if (g.system->id != wanted->systemId)
                continue;
            if (!chosen && g.title == wanted->title)
                chosen = &g;
            only = &g;
            ++count;
        }
        if (!chosen && count == 1)
            chosen = only;
        m_online.match = chosen ? chosen->path : fs::path();
        m_session.setLocalStatus(chosen ? GameStatus::Ready : GameStatus::Missing);
        return;
    }
    if (m_session.localStatus() == GameStatus::Ready || m_session.localStatus() == GameStatus::Missing ||
        m_session.localStatus() == GameStatus::Different)
        return; // déjà vérifié (revérifié si l'hôte change de jeu)

    // On ne calcule l'empreinte que des fichiers de la même console ET de
    // la même taille : inutile de lire 50 Go de jeux pour en trouver un.
    bool pending = false;
    bool sameTitle = false;
    for (const Game& g : m_library.games()) {
        if (g.system->id != wanted->systemId)
            continue;
        if (g.title == wanted->title)
            sameTitle = true;
        if (g.size != wanted->size)
            continue;
        if (const std::optional<uint64_t> hash = m_hasher.request(g.path)) {
            if (*hash == wanted->hash) {
                m_online.match = g.path;
                m_session.setLocalStatus(GameStatus::Ready);
                return;
            }
        } else {
            pending = true;
        }
    }
    if (pending)
        m_session.setLocalStatus(GameStatus::Checking);
    else
        m_session.setLocalStatus(sameTitle ? GameStatus::Different : GameStatus::Missing);
}

void App::updateServices()
{
    if (std::optional<std::string> done = m_portableCopy.takeResult())
        m_ui.notify(*done);
    // Chat vocal. La touche "appuyer pour parler" ne compte pas pendant la
    // saisie d'un texte (un message du salon qui contient un "t"...).
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const int key = m_settings.voiceKey;
    const bool talkKey = key > 0 && key < SDL_SCANCODE_COUNT && keys[key] && !ImGui::GetIO().WantTextInput;
    m_voice.update(m_session, m_settings, talkKey);

    // Mise à jour des cores (jamais celui du jeu en cours).
    m_coreUpdater.update(m_installer, m_coreInUse);
    for (const std::string& message : m_coreUpdater.takeMessages())
        m_ui.notify(message);

    // Mise à jour de Nexo Retro.
    updateAppUpdates();

    // Discord.
    m_discord.configure(m_settings.discordPresence,
                        m_settings.discordAppId.empty() ? std::string(kDiscordAppId) : m_settings.discordAppId);
    m_discord.set(discordActivity());
}

bool App::playingOnline() const
{
    return m_session.inSalon() || m_netplay.active() || m_link.active();
}

void App::updateAppUpdates()
{
    using State = AppUpdater::State;
    AppUpdater& updater = m_appUpdater;
    const State before = updater.state();
    updater.update();
    const State state = updater.state();
    const bool ignored = !updater.latestVersion().empty() && updater.latestVersion() == m_settings.appIgnoredVersion;

    if (updater.takeCheckFinished()) {
        // La date de la vérification, même ratée : la suivante sera demain.
        m_settings.appLastCheck = now();
        m_settings.save(dir("settings.ini"));
        // Trouvée toute seule au démarrage : on prévient (sauf version ignorée).
        // Le bouton "Rechercher" affiche son résultat dans l'onglet lui-même.
        if (state == State::Available && updater.automatic() && !ignored) {
            m_ui.notify("Nexo Retro " + updater.latestVersion() + " est disponible");
            // "Installer automatiquement" : on télécharge déjà, en arrière-plan.
            if ((m_settings.appAutoInstall && updater.installedCopy()) || m_testAppUpdate == "download" ||
                m_testAppUpdate == "install")
                updater.download();
            if (m_testAppUpdate == "install")
                m_installWhenReady = true; // (tests : comme un clic sur "Installer maintenant")
        }
    }

    // Fin d'un téléchargement.
    if (before == State::Downloading && state != State::Downloading) {
        if (state == State::Ready && m_installWhenReady) {
            m_installWhenReady = false;
            installAppUpdate();
        } else if (state == State::Ready && m_settings.appAutoInstall && updater.installedCopy()) {
            m_ui.notify("Nexo Retro " + updater.latestVersion() + " sera installé quand vous quitterez l'application");
        } else if (state == State::Ready) {
            m_ui.notify("Nexo Retro " + updater.latestVersion() + " est prêt à être installé");
        } else if (state == State::Error) {
            m_installWhenReady = false;
            m_ui.notify(updater.error());
        }
    }
}

void App::installAppUpdate()
{
    // Installer veut dire fermer l'appli : jamais au milieu d'une partie en
    // ligne (les amis seraient déconnectés sans prévenir).
    if (playingOnline()) {
        m_ui.notify("Indisponible pendant une partie en ligne");
        return;
    }
    AppUpdater& updater = m_appUpdater;
    // Copie portable ou de développement : pas d'installateur, la page web.
    if (!updater.installedCopy()) {
        if (!updater.pageUrl().empty())
            SDL_OpenURL(updater.pageUrl().c_str());
        return;
    }
    switch (updater.state()) {
    case AppUpdater::State::Available:
    case AppUpdater::State::Error:
        // Pas encore téléchargée : on télécharge, et on installe dès que c'est prêt.
        m_installWhenReady = true;
        updater.download();
        break;
    case AppUpdater::State::Downloading:
        m_installWhenReady = true;
        break;
    case AppUpdater::State::Ready:
        if (updater.scheduleInstall()) {
            // Fermeture normale (le jeu en cours, s'il y en a un, est
            // sauvegardé) ; l'installateur part à la toute fin (main.cpp).
            SDL_Log("Mise à jour de l'appli : fermeture pour installer la version %s", updater.latestVersion().c_str());
            m_running = false;
        } else {
            m_ui.notify(updater.error());
        }
        break;
    default:
        break;
    }
}

DiscordPresence::Activity App::discordActivity() const
{
    // [C++] lang::tr renvoie la traduction (dans la langue de l'interface) :
    // Discord affiche le texte tel quel, sans passer par notre dessin.
    auto tr = [](const std::string& text) { return std::string(lang::tr(text)); };
    DiscordPresence::Activity a;
    const int players = static_cast<int>(m_session.members().size());
    const std::string playersText = tr(std::to_string(players) + (players > 1 ? " joueurs" : " joueur"));
    std::string online;
    if (m_netplay.active() || m_link.active())
        online = " · " + tr("En ligne") + " (" + playersText + ")";
    auto describe = [&](const fs::path& rom, const std::string& title, const std::string& system, long long start) {
        a.details = title;
        a.state = system + online;
        a.startTime = start;
        a.imageText = system;
        if (const scrape::GameInfo* info = m_scraper.find(rom))
            a.image = scrape::boxartUrl(*info);
    };
    if (m_mode == Mode::Playing && m_core) {
        describe(m_romPath, m_gameTitle, m_systemName, m_playStart);
    } else if (m_external) {
        const Game* game = m_library.find(m_externalGame);
        describe(m_externalGame, game ? game->title : toUtf8(m_externalGame.stem()), game ? game->system->name : "",
                 m_externalStart);
    } else if (m_session.inSalon()) {
        a.details = tr("Dans un salon en ligne");
        a.state = playersText;
    } else {
        const size_t games = m_library.games().size();
        a.details = tr("Dans la bibliothèque");
        a.state = tr(std::to_string(games) + (games > 1 ? " jeux" : " jeu"));
    }
    return a;
}

void App::updateNetTest(const net::Session::Event* event)
{
    using Type = net::Session::Event::Type;
    const bool host = !m_options.netTestHost.empty();
    const bool join = !m_options.netTestJoin.empty();
    if (!host && !join)
        return;
    const Uint64 now = SDL_GetTicks();
    if (m_netTestQuitAt && now > m_netTestQuitAt)
        m_running = false;
    // Partie de test : touches simulées, et capture d'écran à la même image chez tous.
    if (m_options.netTestPlayFrames > 0) {
        m_netTestScript = true;
        if (m_netplay.active() || m_link.active())
            m_netTestStarted = true;
        // Câble link : chacun sa partie ; l'invité s'arrête quand l'hôte arrête.
        if (join && m_netTestStarted && m_options.netTestLink && !m_link.active() && !m_netTestQuitAt) {
            SDL_Log("TEST CÂBLE : invité, partie terminée après %d images", m_linkFrames);
            m_netTestQuitAt = now + 500;
        }
        if (m_netplay.running() && m_netplay.frame() == static_cast<uint32_t>(m_options.netTestPlayFrames) &&
            !m_options.exitScreenshot.empty()) {
            m_video.requestScreenshot(m_options.exitScreenshot);
            SDL_Log("TEST NETPLAY : capture à l'image %u", m_netplay.frame());
        }
        if (join && m_netTestStarted && !m_options.netTestLink && !m_netplay.active() && !m_netTestQuitAt) {
            SDL_Log("TEST NETPLAY : invité, partie terminée, %d retours en arrière (le plus profond : %d images)",
                    m_netplay.rollbacks(), m_netplay.deepestRollback());
            m_netTestQuitAt = now + 500;
        }
    }
    // Assez de temps pour la partie demandée (une image = 1/60 s), plus la mise en route.
    if (now > 60000 + static_cast<Uint64>(std::max(0, m_options.netTestPlayFrames)) * 20 && !m_netTestQuitAt) {
        SDL_Log("TEST RÉSEAU : ÉCHEC (délai dépassé)");
        m_running = false;
    }

    if (host) {
        if (m_session.state() == net::Session::State::Idle && !m_netTestCodeWritten && !m_netTestQuitAt) {
            // --net-relay-code <fichier> : héberger par le relais dont le code est dans ce fichier.
            std::string relayCode;
            if (!m_options.netTestRelayCode.empty())
                std::getline(std::ifstream(m_options.netTestRelayCode), relayCode);
            if (!relayCode.empty())
                m_session.hostViaRelay("Salon de test", m_options.netTestPassword, relayCode);
            else
                m_session.host("Salon de test", m_options.netTestPassword);
        }
        if (m_session.state() == net::Session::State::Hosting && !m_netTestCodeWritten) {
            std::ofstream(m_options.netTestHost) << m_session.inviteCode();
            m_netTestCodeWritten = true;
            SDL_Log("TEST RÉSEAU : code écrit (%s)", m_session.inviteCode().c_str());
        }
        const int friends = static_cast<int>(m_session.members().size()) - 1;
        // Quand tout le monde est là : choisir le jeu.
        if (m_options.netTestGame && !m_netTestGamePicked && friends >= m_options.netTestExpect &&
            !m_library.games().empty()) {
            if (m_options.netTestNight) {
                // Soirée jeu : les trois premiers jeux, le premier choisi tout de suite.
                for (size_t i = 0; i < std::min<size_t>(3, m_library.games().size()); ++i)
                    m_online.night.push_back(m_library.games()[i].path);
                ui::advanceGameNight(m_context);
            } else {
                m_context.chooseSalonGame(m_library.games().front());
            }
            m_netTestGamePicked = true;
        }
        // Test du câble link : le salon passe en mode câble dès que le jeu est annoncé.
        // Sinon, partie partagée (netplay), même pour une console portable.
        if (m_session.game() && m_session.game()->link != m_options.netTestLink)
            m_context.setLinkMode(m_options.netTestLink);
        if (event && event->type == Type::Chat && event->text == "bonjour")
            ++m_netTestHellos;
        // Statuts de chacun, une fois tous connus.
        if (m_netTestGamePicked && !m_netTestStatusesLogged && m_session.game()) {
            bool known = true;
            std::string list;
            for (const net::Member& m : m_session.members()) {
                known = known && m.status != net::GameStatus::Unknown && m.status != net::GameStatus::Checking;
                list += m.name + " = " + ui::statusLabel(m.status) + " ; ";
            }
            if (known) {
                SDL_Log("TEST RÉSEAU : statuts du jeu « %s » : %s", m_session.game()->title.c_str(), list.c_str());
                m_netTestStatusesLogged = true;
            }
        }
        const bool gameDone = !m_options.netTestGame || m_netTestStatusesLogged;
        // Test de partie en ligne : on lance dès que tout le monde est prêt.
        if (m_options.netTestPlayFrames > 0) {
            if (gameDone && m_netTestHellos >= m_options.netTestExpect && !m_netTestStarted) {
                m_netTestStarted = true;
                m_context.startNetplay();
            }
            if (m_link.running() && m_linkFrames >= m_options.netTestPlayFrames && !m_netTestQuitAt) {
                SDL_Log("TEST CÂBLE : hôte, %d images avec le câble branché", m_linkFrames);
                m_link.stop(m_session, "Fin du test");
                m_netTestQuitAt = now + 1500;
            }
            if (m_netplay.running() && m_netplay.frame() >= static_cast<uint32_t>(m_options.netTestPlayFrames) + 30 &&
                !m_netTestQuitAt) {
                SDL_Log("TEST NETPLAY : hôte, %u images, délai %d, rollback max %d, %d retours en arrière "
                        "(le plus profond : %d images), %d désynchronisation(s), %d resynchronisation(s)",
                        m_netplay.frame(), m_netplay.delay(), m_netplay.rollbackFrames(), m_netplay.rollbacks(),
                        m_netplay.deepestRollback(), m_netplay.desyncs(), m_netplay.resyncs());
                m_netplay.stop(m_session, "Fin du test");
                m_netTestQuitAt = now + 1500;
            }
        }
        if (!m_netTestQuitAt && m_netTestHellos >= m_options.netTestExpect && gameDone && m_options.netTestPlayFrames == 0) {
            m_session.sendChat("salut");
            SDL_Log("TEST RÉSEAU : hôte OK, %d ami(s) dans le salon", friends);
            m_netTestQuitAt = now + 2000; // le temps que "salut" arrive
            if (!m_options.exitScreenshot.empty())
                m_video.requestScreenshot(m_options.exitScreenshot);
        }
    } else {
        if (!m_netTestJoined) {
            std::ifstream in(m_options.netTestJoin);
            std::string code;
            if (std::getline(in, code) && !code.empty()) {
                m_session.join(code, m_options.netTestPassword);
                m_netTestJoined = true;
            }
        }
        if (event && event->type == Type::Joined)
            m_session.sendChat("bonjour");
        if (event && event->type == Type::Closed) {
            SDL_Log("TEST RÉSEAU : refusé ou fermé : %s", event->text.c_str());
            m_netTestQuitAt = now + 200;
        }
        if (event && event->type == Type::Chat && event->text == "salut") {
            SDL_Log("TEST RÉSEAU : invité OK (« %s » de %s)", event->text.c_str(), event->author.c_str());
            m_netTestQuitAt = now + 1500;
            if (!m_options.exitScreenshot.empty())
                m_video.requestScreenshot(m_options.exitScreenshot);
        }
    }
}

void App::drawFps()
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f images/s", m_fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ts = draw::measure(theme::Weight::Bold, 13.0f, text);
    const ImVec2 min(theme::px(16.0f), theme::px(16.0f));
    draw::rect(dl, min, ImVec2(min.x + ts.x + theme::px(24.0f), min.y + theme::px(30.0f)), theme::withAlpha(theme::Ink, 190),
               theme::px(15.0f));
    draw::text(dl, ImVec2(min.x + theme::px(12.0f), min.y + (theme::px(30.0f) - ts.y) / 2), theme::Weight::Bold, 13.0f,
               theme::Surface, text);
}

void App::drawNetplayOverlay()
{
    using theme::px;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // En haut à droite : "En ligne · Joueur 2 · rollback 6" (ou "délai 4").
    const std::string mode = m_netplay.rollbackFrames() > 0 ? "rollback " + std::to_string(m_netplay.rollbackFrames())
                                                            : "délai " + std::to_string(m_netplay.delay());
    const std::string badge = "En ligne · Joueur " + std::to_string(m_netplay.localPort() + 1) + " · " + mode;
    const ImVec2 bs = draw::measure(theme::Weight::Bold, 13.0f, badge);
    const ImVec2 bmin(vp->Pos.x + vp->Size.x - bs.x - px(48.0f), vp->Pos.y + px(16.0f));
    draw::rect(dl, bmin, ImVec2(bmin.x + bs.x + px(32.0f), bmin.y + px(30.0f)), theme::withAlpha(theme::Ink, 190), px(15.0f));
    dl->AddCircleFilled(ImVec2(bmin.x + px(14.0f), bmin.y + px(15.0f)), px(4.0f), theme::rgb(0x34D399));
    draw::text(dl, ImVec2(bmin.x + px(24.0f), bmin.y + (px(30.0f) - bs.y) / 2), theme::Weight::Bold, 13.0f, theme::Surface,
               badge);

    // Au centre : chargement, synchronisation ou attente d'un joueur.
    const std::string status = m_netplay.statusText();
    if (status.empty())
        return;
    const ImVec2 ts = draw::measure(theme::Weight::Bold, 16.0f, status);
    const ImVec2 size(ts.x + px(48.0f), px(56.0f));
    const ImVec2 min(vp->Pos.x + (vp->Size.x - size.x) / 2, vp->Pos.y + (vp->Size.y - size.y) / 2);
    draw::shadow(dl, min, ImVec2(min.x + size.x, min.y + size.y), size.y / 2, px(18.0f), px(8.0f), 60);
    draw::rect(dl, min, ImVec2(min.x + size.x, min.y + size.y), theme::Surface, size.y / 2);
    draw::text(dl, ImVec2(min.x + px(24.0f), min.y + (size.y - ts.y) / 2), theme::Weight::Bold, 16.0f, theme::Ink, status);
}

// --- Câble link ---------------------------------------------------------------
// Chacun lance SON jeu (avec sa sauvegarde, comme une partie normale), puis
// on relie les consoles : par les paquets du core (gpSP), ou par le relais
// du câble Game Boy (Gambatte).

namespace {
constexpr uint16_t kGameLinkServerPort = 56400; // Gambatte de l'hôte (serveur)
constexpr uint16_t kGameLinkRelayPort = 56401;  // relais de l'ami (Gambatte client s'y connecte)
} // namespace

void App::updateLink()
{
    using Transport = net::LinkPlay::Transport;
    if (m_link.wantsLaunch()) {
        const Game* game = m_online.match.empty() ? nullptr : m_library.find(m_online.match);
        const bool gameLink = m_link.transport() == Transport::GameLink;
        const auto* console = game ? net::LinkPlay::consoleLink(game->system->id) : nullptr;
        const char* coreFile = console ? console->coreFile : "";
        std::error_code ec;
        if (!game || !console) {
            m_link.launched(m_session, false, "", "aucun jeu choisi pour cette console");
        } else if (!fs::exists(dir("cores") / coreFile, ec)) {
            m_link.launched(m_session, false, "", std::string(coreFile) + " est manquant");
        } else {
            if (gameLink) {
                // Le relais d'abord : chez l'ami, Gambatte s'y connecte dès son démarrage.
                const bool host = m_link.isHost();
                m_tunnel = std::make_unique<net::LinkTunnel>(
                    host ? net::LinkTunnel::Role::ConnectToCore : net::LinkTunnel::Role::ListenForCore,
                    host ? kGameLinkServerPort : kGameLinkRelayPort);
                m_tunnelAddressesSent = false;
                // Les options de Gambatte : hôte serveur, ami client de son relais (127.0.0.1).
                m_launchOptions["gambatte_gb_link_mode"] = host ? "Network Server" : "Network Client";
                m_launchOptions["gambatte_gb_link_network_port"] =
                    std::to_string(host ? kGameLinkServerPort : kGameLinkRelayPort);
                const char digits[] = "127000000001"; // 127.000.000.001, un chiffre par option
                for (int i = 0; i < 12; ++i)
                    m_launchOptions["gambatte_gb_link_network_server_ip_" + std::to_string(i + 1)] = std::string(1, digits[i]);
            }
            // Une partie normale : la sauvegarde du joueur est chargée et gardée.
            if (!launch(dir("cores") / coreFile, game->path, {})) {
                m_link.launched(m_session, false, "",
                                m_launchError.empty() ? std::string("le core n'a pas démarré") : m_launchError);
            } else if (!gameLink && !m_core->hasNetpacket()) {
                m_link.launched(m_session, false, "", "cette version du core ne gère pas le câble link");
            } else {
                m_link.launched(m_session, true, gameLink ? m_core->coreInfo() : m_core->netpacketVersion(), "");
            }
        }
    }

    // Câble Game Boy : échanger les adresses des deux relais.
    if (m_tunnel) {
        if (!m_tunnelAddressesSent && m_link.active()) {
            if (const auto addresses = m_tunnel->addresses()) {
                m_link.sendTunnelAddresses(m_session, *addresses);
                m_tunnelAddressesSent = true;
            }
        }
        if (const auto peer = m_link.takePeerAddresses())
            m_tunnel->setPeer(*peer);
    }

    // Départ : on branche le câble du core (gpSP).
    if (m_link.takeStarted() && m_core && m_link.transport() == Transport::Netpacket) {
        // Pendant l'image, si le core réclame ses paquets : envoyer ce qui
        // attend, lire le réseau, livrer ce qui est arrivé. Les autres
        // événements du salon sont gardés pour la boucle habituelle.
        m_core->setNetpacketPump([this] {
            for (const Core::NetPacket& p : m_core->takeNetpackets())
                m_link.send(m_session, p.to, p.data);
            m_session.update();
            for (net::Session::Event& e : m_session.takeEvents()) {
                if (e.type == net::Session::Event::Type::AppMessage && net::LinkPlay::isLinkMessage(e.data))
                    m_link.onAppMessage(m_session, e.from, e.data);
                else
                    m_deferredEvents.push_back(std::move(e));
            }
            for (net::LinkPlay::Received& r : m_link.takeReceived())
                m_core->netpacketReceive(r.data, r.from);
        });
        m_core->netpacketStart(m_link.localId());
        if (m_link.isHost())
            for (const net::LinkPlay::Player& p : m_link.players())
                if (p.linkId != 0)
                    m_core->netpacketConnected(p.linkId);
        m_ui.notify("Câble link branché");
    }

    for (const std::string& message : m_link.takeMessages()) {
        m_online.chat.push_back({"", message, false});
        m_ui.notify(message);
    }
    // Fin : le câble est débranché, mais chacun continue SA partie.
    if (m_link.takeStopped()) {
        if (m_core)
            m_core->netpacketStop();
        m_tunnel.reset();
    }
}

void App::drawLinkOverlay()
{
    using theme::px;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    std::string badge = "Câble link";
    if (!m_link.running())
        badge += " · démarrage…";
    else if (m_tunnel)
        badge = m_tunnel->status();
    else
        badge += " · " + std::to_string(m_link.players().size()) + " joueurs";
    const ImVec2 bs = draw::measure(theme::Weight::Bold, 13.0f, badge);
    const ImVec2 bmin(vp->Pos.x + vp->Size.x - bs.x - px(48.0f), vp->Pos.y + px(16.0f));
    draw::rect(dl, bmin, ImVec2(bmin.x + bs.x + px(32.0f), bmin.y + px(30.0f)), theme::withAlpha(theme::Ink, 190), px(15.0f));
    dl->AddCircleFilled(ImVec2(bmin.x + px(14.0f), bmin.y + px(15.0f)), px(4.0f), theme::rgb(0x34D399));
    draw::text(dl, ImVec2(bmin.x + px(24.0f), bmin.y + (px(30.0f) - bs.y) / 2), theme::Weight::Bold, 13.0f, theme::Surface,
               badge);
}

// Sous le badge "En ligne" : qui parle, et (si demandé) le ping et les
// pertes de chacun.
void App::drawOnlineStats()
{
    using theme::px;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + px(54.0f);
    for (const net::Member& m : m_session.members()) {
        if (m.self)
            continue;
        const bool talking = m_voice.talking(m.id);
        if (!m_settings.netStats && !talking)
            continue;
        std::string line = m.name;
        if (m_settings.netStats) {
            line += "  ·  " + (m.pingMs >= 0 ? std::to_string(m.pingMs) + " ms" : std::string("…"));
            if (m.lossPct >= 0)
                line += "  ·  " + std::to_string(m.lossPct) + " % " + std::string(lang::tr("de pertes"));
        }
        ImU32 dot = theme::rgb(0x9CA3AF);
        switch (net::qualityOf(m.pingMs, m.lossPct)) {
        case net::LinkQuality::Excellent:
        case net::LinkQuality::Good: dot = theme::rgb(0x34D399); break;
        case net::LinkQuality::Fair: dot = theme::rgb(0xFBBF24); break;
        case net::LinkQuality::Poor: dot = theme::rgb(0xF87171); break;
        case net::LinkQuality::Unknown: break;
        }
        const ImVec2 ts = draw::measure(theme::Weight::Bold, 12.0f, line);
        const float iconW = talking ? px(20.0f) : 0.0f;
        const float w = ts.x + iconW + px(36.0f);
        const ImVec2 min(vp->Pos.x + vp->Size.x - w - px(16.0f), y);
        draw::rect(dl, min, ImVec2(min.x + w, min.y + px(26.0f)), theme::withAlpha(theme::Ink, talking ? 220 : 170), px(13.0f));
        dl->AddCircleFilled(ImVec2(min.x + px(13.0f), min.y + px(13.0f)), px(4.0f), m_settings.netStats ? dot : theme::rgb(0x34D399));
        if (talking)
            draw::icon(dl, draw::Icon::Mic, ImVec2(min.x + px(22.0f), min.y + px(5.0f)), px(16.0f), theme::rgb(0x34D399));
        draw::text(dl, ImVec2(min.x + px(24.0f) + iconW, min.y + (px(26.0f) - ts.y) / 2), theme::Weight::Bold, 12.0f,
                   theme::Surface, line);
        y += px(32.0f);
    }
    // Son propre micro coupé : un rappel discret.
    if (m_voice.selfMuted() && m_settings.voiceMode != 0) {
        const std::string muted(lang::tr("Micro coupé"));
        const ImVec2 ts = draw::measure(theme::Weight::Bold, 12.0f, muted);
        const ImVec2 min(vp->Pos.x + vp->Size.x - ts.x - px(56.0f), y);
        draw::rect(dl, min, ImVec2(min.x + ts.x + px(40.0f), min.y + px(26.0f)), theme::withAlpha(theme::Ink, 170), px(13.0f));
        draw::icon(dl, draw::Icon::MicOff, ImVec2(min.x + px(9.0f), min.y + px(5.0f)), px(16.0f), theme::rgb(0xF87171));
        draw::text(dl, ImVec2(min.x + px(30.0f), min.y + (px(26.0f) - ts.y) / 2), theme::Weight::Bold, 12.0f, theme::Surface,
                   muted);
    }
}

bool App::launch(const fs::path& corePath, const fs::path& romPath, const fs::path& state, bool netplay, bool keepSave)
{
    closeGame();
    m_input.clearOverride();

    // Chaque core peut demander un OpenGL différent : on repart d'un
    // contexte neuf. L'interface et les jaquettes doivent lâcher l'ancien
    // avant qu'il disparaisse.
    m_ui.shutdownRenderer();
    m_covers.releaseTextures();
    m_video.destroyContext();

    auto core = std::make_unique<Core>(m_video, m_audio, m_input);
    core->setNetplay(netplay);
    // Vulkan si l'utilisateur le préfère, et d'office pour PCSX2 : en OpenGL,
    // il compile ses effets en plein jeu, et les pilotes AMD figent alors
    // l'image un instant (saccades). L'équipe de PCSX2 conseille Vulkan.
    // Dolphin, lui, reste en OpenGL : en Vulkan, il ne nous envoie aucune image.
    const bool pcsx2 = corePath.stem() == "pcsx2_libretro";
    const bool dolphin = corePath.stem() == "dolphin_libretro";
    core->setPreferVulkan((m_settings.preferVulkan || pcsx2) && !dolphin && VulkanRender::available());
    // Les options de l'utilisateur, plus celles imposées pour cette partie
    // (câble link : réglages réseau de Gambatte).
    std::map<std::string, std::string> options = readCoreOptions(dir("core-options.txt"));
    // Wii : viser avec la souris (Paramètres > Manettes) plutôt qu'au stick droit.
    if (dolphin && m_settings.wiiMousePointer)
        options["dolphin_ir_mode"] = "2";
    for (const auto& [key, value] : m_launchOptions)
        options[key] = value;
    m_launchOptions.clear();
    core->setOptionOverrides(std::move(options));
    core->setSystemDirectory(toUtf8(dir("bios")));
    // En ligne, le core range ses fichiers à lui (réglages des bornes
    // d'arcade, NVRAM...) dans un dossier vide à chaque partie : sinon chaque
    // PC démarre avec SES réglages (difficulté, options de la borne...) et
    // les parties divergent dès le démarrage (bug vu sur Marvel vs Capcom :
    // pas le même décor chez les deux joueurs). Sauf l'hôte qui part
    // volontairement de sa sauvegarde.
    fs::path saveDir = dir("saves");
    if (netplay && !keepSave) {
        saveDir /= "en-ligne";
        std::error_code ec;
        fs::remove_all(saveDir, ec);
        fs::create_directories(saveDir, ec);
    }
    core->setSaveDirectory(toUtf8(saveDir));

    // Core::loadGame crée lui-même le contexte OpenGL adapté au core.
    if (!core->load(corePath) || !core->loadGame(romPath)) {
        m_launchError = core->loadError();
        core.reset(); // détruit le core maintenant, tant que la vidéo existe
        restartGraphics();
        m_ui.notify(m_launchError.empty() ? "Impossible de lancer le jeu (détails dans NexoRetro.log)"
                                          : "Impossible de lancer le jeu : " + m_launchError);
        return false;
    }
    m_core = std::move(core);
    m_ui.initRenderer();
    // On retient la liste des options du core (pour l'écran des options).
    if (!m_core->optionList().empty())
        saveOptionList(dir("core-options") / fromUtf8(toUtf8(corePath.stem()) + ".txt"), m_core->optionList());

    const std::string stem = romPath.empty() ? m_core->info().library_name : toUtf8(romPath.stem());
    const Game* game = romPath.empty() ? nullptr : m_library.find(romPath);
    m_romPath = romPath;
    m_gameTitle = romPath.empty() ? stem : cleanTitle(stem);
    m_systemName = game ? game->system->name : m_core->info().library_name;
    // Wii : la manette choisie pour ce jeu (Wiimote + Nunchuk par défaut),
    // branchée sur les 4 ports (les autres joueurs ont la même).
    if (game && std::string_view(game->system->id) == "wii") {
        const GameRecord* record = m_userData.find(game->path);
        const std::string wanted = record && !record->controller.empty() ? record->controller
                                                                         : std::string(kDefaultWiiController);
        const auto& ports = m_core->controllerTypes();
        for (unsigned port = 0; port < ports.size() && port < 4; ++port)
            for (const Core::ControllerType& type : ports[port])
                if (type.name == wanted) {
                    m_core->setControllerType(port, type.id);
                    break;
                }
        SDL_Log("Manette Wii : %s", wanted.c_str());
        m_remapKey = "wii:" + wanted; // chaque manette Wii a ses propres commandes
    } else {
        m_remapKey = game ? std::string(game->system->id) : toUtf8(corePath.stem());
    }
    // Les commandes propres à cette console (menu pause > Commandes de la console).
    m_input.setRemap(m_remaps.get(m_remapKey));
    // Le retour en arrière repart de zéro pour ce jeu.
    m_rewind.reset();
    clearRewindThumbnails();
    m_rewindUsable = true;
    m_gameLoops = 0;
    // Le dossier de sauvegarde du jeu : saves/<nom>/ (créé au besoin).
    const SaveFiles files = saveFilesFor(dir("saves"), stem);
    std::error_code dirError;
    fs::create_directories(files.dir, dirError);
    m_gameSaveDir = files.dir;
    m_sramFile = files.sram;
    m_stateFile = files.state;
    m_autoStateFile = files.autoState;
    // En ligne, on part de l'état envoyé par l'hôte : la sauvegarde du
    // joueur n'est ni chargée, ni écrasée en fin de partie. Sauf pour l'hôte
    // qui a choisi de partir de SA progression : il la charge (puis l'état
    // de départ, envoyé à tous, la contient), et l'enregistre à la fin.
    m_netplayGame = netplay;
    m_netplayKeepsSave = netplay && keepSave;
    const bool useSave = !netplay || keepSave;
    if (useSave)
        m_core->loadSram(m_sramFile);
    // La sauvegarde d'état n'est PAS rechargée tout de suite : certains cores
    // ne démarrent leur machine qu'à la première image (PCSX2 : processeurs,
    // carte graphique...). Recharger un état avant les faisait planter
    // quelques images plus tard. Voir applyPendingState().
    m_pendingState = (!state.empty() && useSave) ? state : fs::path();
    m_framesSinceLaunch = 0;

    // Codes de triche activés pour ce jeu. Jamais sur une console partagée en
    // ligne : un seul joueur tricheur, et les parties divergeraient.
    if (!netplay && !romPath.empty()) {
        std::vector<std::pair<std::string, bool>> codes;
        for (const Cheat& c : loadCheats(dir("cheats") / fromUtf8(stem + ".txt")))
            codes.emplace_back(c.code, c.enabled);
        if (const int n = m_core->applyCheats(codes); n > 0)
            m_ui.notify(std::to_string(n) + (n > 1 ? " codes de triche actifs" : " code de triche actif"));
    }

    // Historique : date de la partie (le temps de jeu est compté à la fin).
    m_playStart = now();
    m_coreInUse = toUtf8(corePath.filename());
    if (!romPath.empty()) {
        m_userData.record(romPath).lastPlayed = m_playStart;
        m_userData.save();
    }

    const std::string title = m_gameTitle + " - Nexo Retro";
    m_video.setTitle(title.c_str());
    if (m_settings.fullscreenOnLaunch || m_nav.bigPicture)
        m_video.setFullscreen(true);
    m_mode = Mode::Playing;
    setMenuOpen(false);
    // Le chrono suit le jeu lancé ; lancé depuis l'écran Speedrun, il est déjà affiché.
    m_speedrun.setGame(romPath, m_gameTitle);
    m_speedrun.setEnabled(m_nav.speedrunLaunch && !romPath.empty());
    m_nav.speedrunLaunch = false;
    m_ui.notify(m_settings.menuButton == 0 ? "Échap ou Select + Start : menu" : "Échap ou bouton central : menu");
    return true;
}

void App::closeGame()
{
    if (!m_core)
        return;
    // Quitter son jeu débranche le câble pour tout le monde.
    if (m_link.active())
        m_link.stop(m_session, m_settings.nickname + " a quitté la partie");
    m_core->netpacketStop();
    m_tunnel.reset();
    // Quitté avant que la partie reprise soit rechargée : le jeu n'a pas
    // vraiment commencé, on ne touche pas aux sauvegardes (sinon l'écran de
    // démarrage écraserait la vraie partie).
    const bool notResumedYet = !m_pendingState.empty();
    m_pendingState.clear();
    if ((!m_netplayGame || m_netplayKeepsSave) && !notResumedYet) {
        m_core->saveSram(m_sramFile);
        if (m_settings.autoSaveState)
            m_core->saveState(m_autoStateFile);
    }
    m_netplayGame = false;
    m_netplayKeepsSave = false;
    m_speedrun.reset();         // (une run en cours compte comme abandonnée)
    updateSpeedrun();           // ... et sa vidéo est terminée
    m_speedrun.setGame({}, {});
    m_input.clearOverride();
    m_input.setRemap(Remaps::identity()); // hors du jeu : chaque bouton à lui-même
    m_controlsOpen = false;
    m_slotsOpen = false;
    if (!m_romPath.empty()) {
        GameRecord& record = m_userData.record(m_romPath);
        record.lastPlayed = now();
        record.playSeconds += now() - m_playStart;
        m_userData.save();
    }
    clearRewindThumbnails();
    m_core.reset();
    m_coreInUse.clear();
    m_audio.clear();
    m_mode = Mode::Interface;
    m_menuOpen = false;
}

void App::returnToInterface()
{
    closeGame();
    restartGraphics();
    // (En Big Picture, on revient sur l'écran Big Picture, toujours en plein écran.)
    if (m_settings.fullscreenOnLaunch && !m_nav.bigPicture)
        m_video.setFullscreen(false);
    const std::string title = "Nexo Retro · " + m_settings.nickname;
    m_video.setTitle(title.c_str());
    m_bios.refresh();
}

// L'icône de la fenêtre et de la barre des tâches (l'exe a aussi la sienne,
// dans src/app.rc : c'est celle que montre l'Explorateur).
void App::setWindowIcon()
{
    const fs::path file = fromUtf8(SDL_GetBasePath()) / "assets" / "logo" / "nexo-256.png";
    SDL_IOStream* io = SDL_IOFromFile(toUtf8(file).c_str(), "rb");
    size_t size = 0;
    void* bytes = io ? SDL_LoadFile_IO(io, &size, true) : nullptr;
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = bytes ? stbi_load_from_memory(static_cast<const stbi_uc*>(bytes), static_cast<int>(size), &w,
                                                          &h, &channels, 4)
                                  : nullptr;
    SDL_free(bytes);
    if (!pixels) {
        SDL_Log("Icône introuvable : %s", toUtf8(file).c_str());
        return;
    }
    // stb_image donne les octets R, G, B, A dans cet ordre : SDL_PIXELFORMAT_RGBA32.
    if (SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4)) {
        SDL_SetWindowIcon(m_video.window(), surface);
        SDL_DestroySurface(surface);
    }
    stbi_image_free(pixels);
}

bool App::restartGraphics()
{
    m_ui.shutdownRenderer();
    m_covers.releaseTextures();
    m_video.destroyContext();
    return m_video.createContext({}) && m_ui.initRenderer();
}

void App::applySettings()
{
    m_video.setSmooth(m_settings.scaling == 1);
    const float aspects[] = {0.0f, 4.0f / 3.0f, 16.0f / 9.0f};
    m_video.setAspectOverride(aspects[std::clamp(m_settings.aspect, 0, 2)]);
    m_input.setKeyboardSeparate(m_settings.keyboardSeparate);
    m_audio.setVolume(m_settings.volume);
    // L'apparence : thème, couleurs, fond, police (rechargée par Ui::beginFrame).
    theme::applySettings(m_settings);
    m_ui.setZoom(m_settings.uiZoom / 100.0f);
    m_netplay.setHeavyRollback(m_settings.netHeavyRollback);
    // Sons de l'interface : pack choisi, ou celui du thème.
    const int pack = m_settings.uiSoundPack > 0 ? m_settings.uiSoundPack - 1 : theme::active().soundPack;
    sounds::configure(m_settings.uiSounds, m_settings.uiSoundVolume, static_cast<sounds::Pack>(pack));
    // La langue de l'interface (fichiers assets/lang/<code>.txt).
    if (lang::current() != m_settings.language)
        lang::setLanguage(fromUtf8(SDL_GetBasePath()) / "assets" / "lang", m_settings.language);
    for (const System& s : Library::systems())
        if (s.external()) {
            const auto it = m_settings.externalPaths.find(s.id);
            m_library.setExternalPath(s.id, it == m_settings.externalPaths.end() ? fs::path() : fromUtf8(it->second));
        }
}

// --- Émulateurs externes ---------------------------------------------------------

bool App::launchExternal(const Game& game)
{
    const System& system = *game.system;
    const fs::path exe = m_library.externalPath(system);
    if (exe.empty()) {
        m_ui.notify(std::string("Indiquez où est ") + system.externalName + " : Paramètres > Émulateurs externes");
        return false;
    }
    if (m_external && m_external->running()) {
        m_ui.notify("Un jeu est déjà ouvert dans un émulateur externe");
        return false;
    }
    // Les arguments : ceux choisis dans Paramètres, sinon ceux par défaut.
    // {rom} est remplacé par le chemin du jeu (un argument à lui seul, même
    // s'il contient des espaces).
    const auto custom = m_settings.externalArgs.find(system.id);
    const std::string pattern = custom != m_settings.externalArgs.end() && !custom->second.empty() ? custom->second
                                                                                                   : system.externalArgs;
    std::vector<std::string> args;
    std::istringstream words(pattern);
    std::string word;
    while (words >> word) {
        const size_t at = word.find("{rom}");
        args.push_back(at == std::string::npos ? word : word.replace(at, 5, toUtf8(game.path)));
    }
    auto process = std::make_unique<ExternalProcess>();
    std::string error;
    if (!process->start(exe, args, error)) {
        m_ui.notify(std::string(system.externalName) + " : " + error);
        return false;
    }
    m_external = std::move(process);
    m_externalGame = game.path;
    m_externalStart = now();
    m_userData.record(game.path).lastPlayed = m_externalStart;
    m_userData.save();
    m_ui.notify(game.title + " démarre dans " + system.externalName);
    return true;
}

void App::updateExternal()
{
    // Une installation en un clic vient de se terminer : on retient où est
    // le programme, comme s'il avait été choisi dans Paramètres.
    if (const std::optional<ExternalInstaller::Result> installed = m_installer.takeResult();
        installed && installed->systemId.starts_with("core:")) {
        // Un core libretro : il est dans cores/, le jeu peut se lancer. (Les
        // mises à jour automatiques sont annoncées toutes ensemble à la fin.)
        if (!m_coreUpdater.onInstalled(installed->systemId.substr(5), installed->ok))
            m_ui.notify(installed->systemId.substr(5) + " " + installed->message);
    } else if (installed && installed->systemId.starts_with("files:")) {
        // Fichiers système d'un core (Dolphin) : l'écran BIOS les voit tout de suite.
        m_bios.refresh();
        m_ui.notify("Fichiers de " + installed->systemId.substr(6) + (installed->ok ? " installés" : " : " + installed->message));
    } else if (installed) {
        const System* system = Library::systemById(installed->systemId);
        const std::string name = system ? system->externalName : installed->systemId;
        if (installed->ok) {
            m_settings.externalPaths[installed->systemId] = toUtf8(installed->exe);
            external::configure(installed->systemId, installed->exe, dir("bios")); // xemu : ses BIOS déjà rangés
            m_context.settingsChanged();
            m_ui.notify(name + " " + installed->message);
        } else {
            m_ui.notify(name + " : " + installed->message);
        }
    }
    // Une fois par demi-seconde suffit pour voir que l'émulateur est fermé.
    if (!m_external || SDL_GetTicks() - m_externalChecked < 500)
        return;
    m_externalChecked = SDL_GetTicks();
    if (m_external->running())
        return;
    GameRecord& record = m_userData.record(m_externalGame);
    record.playSeconds += now() - m_externalStart;
    record.lastPlayed = now();
    m_userData.save();
    m_external.reset();
}

void App::setMenuOpen(bool open)
{
    m_menuOpen = open;
    m_controlsOpen = false;
    m_slotsOpen = false;
    m_audio.setPaused(open && !m_netplay.active()); // en ligne, le jeu continue
    if (open)
        m_pauseMenu.open();
}

void App::drawControlsScreen()
{
    // Les actions de la console, d'après ce que le core en dit (joueur 1,
    // manette standard) : "Wiimote 1", "Shake", "C-Haut"... Sans description
    // du core, on liste les 16 boutons du RetroPad.
    static const char* const generic[] = {"B", "Y", "Select", "Start", "Haut", "Bas", "Gauche", "Droite",
                                          "A", "X", "L", "R", "L2", "R2", "L3", "R3"};
    // L'ordre d'affichage : boutons d'action, croix, gâchettes, puis Select/Start.
    static const int order[] = {0, 8, 1, 9, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 2, 3};
    std::vector<ui::ControlRow> rows;
    std::array<std::string, 2> sticks; // ce que fait chaque stick (gauche, droit)
    std::array<std::string, 16> described;
    for (const Core::InputDescriptor& d : m_core->inputDescriptors()) {
        if (d.port != 0)
            continue;
        if ((d.device & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD && d.id < 16 && described[d.id].empty())
            described[d.id] = d.description;
        else if ((d.device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG && d.index < 2 &&
                 sticks[d.index].find(d.description) == std::string::npos)
            sticks[d.index] += (sticks[d.index].empty() ? "" : " · ") + d.description;
    }
    const bool any = std::any_of(described.begin(), described.end(), [](const std::string& s) { return !s.empty(); });
    for (int id : order)
        if (!any || !described[id].empty())
            rows.push_back({id, any ? described[id] : std::string("Bouton ") + generic[id]});
    // Les sticks du core : ceux qu'il décrit, ou les deux s'il ne décrit rien.
    for (int i = 0; i < 2; ++i)
        if (!sticks[i].empty() || !any)
            rows.push_back({Remaps::kLeftStick + i, sticks[i].empty() ? std::string(i == 0 ? "Stick gauche" : "Stick droit")
                                                                      : sticks[i]});

    const std::string subtitle = m_systemName + " — pour chaque action, le bouton de votre manette qui la déclenche. "
                                                "Mémorisé pour cette console" +
                                 (m_remapKey.starts_with("wii:") ? std::string(" et cette manette Wii.") : std::string("."));
    const ui::ControlsResult result = m_pauseMenu.drawControls(subtitle, rows, m_remaps.get(m_remapKey));
    if (result.coreButton >= 0)
        m_remaps.set(m_remapKey, result.coreButton, result.source);
    if (result.reset)
        m_remaps.reset(m_remapKey);
    if (result.coreButton >= 0 || result.reset)
        m_input.setRemap(m_remaps.get(m_remapKey));
    if (result.back)
        m_controlsOpen = false;
        m_slotsOpen = false;
    m_slotsOpen = false;
}

void App::handlePauseAction(ui::PauseAction action)
{
    using ui::PauseAction;
    // En ligne, changer l'état d'un seul émulateur désynchroniserait la partie.
    const bool online = m_netplay.active();
    if (online && (action == PauseAction::SaveState || action == PauseAction::LoadState || action == PauseAction::Reset ||
                   action == PauseAction::Slots)) {
        m_ui.notify("Indisponible pendant une partie en ligne");
        return;
    }
    if (online && (action == PauseAction::Library || action == PauseAction::Quit))
        m_netplay.stop(m_session, m_settings.nickname + " a quitté la partie");
    switch (action) {
    case PauseAction::None: break;
    case PauseAction::Resume: setMenuOpen(false); break;
    case PauseAction::Controls: m_controlsOpen = true; break;
    case PauseAction::Slots: m_slotsOpen = true; break;
    case PauseAction::SaveState: saveState(); break;
    case PauseAction::LoadState: loadState(); setMenuOpen(false); break;
    case PauseAction::Reset: m_core->reset(); setMenuOpen(false); break;
    case PauseAction::Screenshot:
        // On ferme le menu et on capture l'image suivante, sans le menu dessus.
        setMenuOpen(false);
        m_screenshotNextFrame = true;
        break;
    case PauseAction::Fullscreen: m_video.toggleFullscreen(); break;
    case PauseAction::Library: m_pendingReturn = true; break;
    case PauseAction::Quit: m_running = false; break;
    case PauseAction::ToggleMic:
        m_voice.setSelfMuted(!m_voice.selfMuted());
        m_ui.notify(m_voice.selfMuted() ? "Micro coupé" : "Micro réactivé");
        break;
    case PauseAction::ToggleNetStats:
        m_settings.netStats = !m_settings.netStats;
        m_settings.save(dir("settings.ini"));
        break;
    }
}

void App::captureRewind()
{
    if (!m_settings.rewind || !m_rewindUsable || !m_core || !m_core->canSerialize())
        return;
    // Au-delà de 16 Mo par photo (PS2, GameCube...), photographier 60 fois
    // par seconde ralentirait le jeu : pas de retour en arrière pour elles.
    constexpr size_t kMaxRewindState = 16u * 1024 * 1024;
    if (m_core->stateSize() > kMaxRewindState) {
        m_rewindUsable = false;
        SDL_Log("Retour en arrière désactivé : état de %zu Mo", m_core->stateSize() / (1024 * 1024));
        return;
    }
    std::string state;
    if (m_core->serialize(state))
        m_rewind.push(state);
}

// --- Speedrun ------------------------------------------------------------------------------

void App::updateParental()
{
    if (!m_settings.parentalOn)
        return;
    const Uint64 ticks = SDL_GetTicks();
    if (m_mode != Mode::Playing) {
        m_parentalTick = ticks;
        m_parentalWarned = false;
        return;
    }
    if (ticks - m_parentalTick < 1000)
        return;
    m_parentalTick = ticks;
    m_settings.rollParentalDay();
    // (Le temps compte aussi menu ouvert : une pause ne se prolonge pas indéfiniment.)
    ++m_settings.parentalUsed;
    if (m_settings.parentalUsed % 30 == 0)
        m_settings.save(dir("settings.ini"));
    const int left = m_settings.parentalSecondsLeft();
    if (left <= 5 * 60 && !m_parentalWarned) {
        m_parentalWarned = true;
        m_ui.notify("Plus que " + std::to_string((left + 59) / 60) + " min de jeu aujourd'hui");
    }
    if (left == 0 && !m_pendingReturn) {
        // La partie est sauvegardée (sauvegarde du jeu et état automatique) en quittant.
        m_settings.save(dir("settings.ini"));
        m_ui.notify("Le temps de jeu d'aujourd'hui est écoulé : la partie est sauvegardée");
        if (m_netplay.active())
            m_netplay.stop(m_session, m_settings.nickname + " a atteint son temps de jeu du jour");
        m_pendingReturn = true;
    }
}

void App::speedrunKey(SDL_Keycode key)
{
    // En ligne, le temps ne s'arrête pas pour un seul joueur : pas de chrono.
    if (m_netplay.active() || m_link.active()) {
        m_ui.notify("Indisponible pendant une partie en ligne");
        return;
    }
    switch (key) {
    case SDLK_F5: m_speedrun.startOrSplit(); break;
    case SDLK_F6: m_speedrun.undoSplit(); break;
    case SDLK_F7: m_speedrun.skipSplit(); break;
    case SDLK_F9:
        m_speedrun.reset();
        // Une nouvelle tentative repart du début du jeu (réglable dans l'écran Speedrun).
        if (m_settings.speedrunResetGame && m_core) {
            m_core->reset();
            m_rewind.reset();
            clearRewindThumbnails();
        }
        m_ui.notify("Chrono remis à zéro");
        break;
    default: break;
    }
}

void App::updateSpeedrun()
{
    // Tests : F5 "appuyée" aux tours de boucle demandés.
    if (!m_options.testSpeedrun.empty() && m_mode == Mode::Playing) {
        std::istringstream list(m_options.testSpeedrun);
        std::string value;
        while (std::getline(list, value, ','))
            if (std::atoi(value.c_str()) == m_gameLoops && m_gameLoops != m_testSpeedrunLoop) {
                m_testSpeedrunLoop = m_gameLoops;
                speedrunKey(SDLK_F5);
            }
    }
    // Une run part : la vidéo démarre avec elle.
    if (m_speedrun.takeStarted() && m_settings.speedrunRecord && m_core && !m_recorder.recording()) {
        const fs::path folder = m_settings.speedrunFolder.empty() ? dir("speedruns") : fromUtf8(m_settings.speedrunFolder);
        const speedrun::Category* cat = m_speedrun.category();
        // "Super Mario World - Any% - 2026-10-24 21-05-33.mp4" (sans les caractères interdits par Windows)
        char date[32];
        const std::time_t t = std::time(nullptr);
        std::tm day{};
        localtime_s(&day, &t);
        std::strftime(date, sizeof(date), "%Y-%m-%d %H-%M-%S", &day);
        std::string name = m_gameTitle + " - " + (cat ? cat->name : std::string("Run")) + " - " + date;
        for (char& c : name)
            if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                c = '_';
        const retro_system_timing& timing = m_core->avInfo().timing;
        if (m_recorder.start(folder / fromUtf8(name + ".mp4"), timing.fps, timing.sample_rate))
            m_audio.setRecorder(&m_recorder);
    }
    // Une run finit (terminée ou remise à zéro) : la vidéo est fermée, gardée ou non.
    if (m_speedrun.takeEnded()) {
        const speedrun::Run* run = m_speedrun.lastRun();
        if (run && run->timeMs >= 0)
            m_ui.notify(m_speedrun.lastWasPb() ? "Nouveau record : " + speedrun::formatTime(run->timeMs) + " !"
                                               : "Run terminée : " + speedrun::formatTime(run->timeMs));
        if (m_recorder.recording()) {
            m_audio.setRecorder(nullptr);
            const fs::path file = m_recorder.file();
            m_recorder.stop();
            const bool finished = run && run->timeMs >= 0;
            const bool keep = m_settings.speedrunKeep == 0 || (m_settings.speedrunKeep == 1 && finished) ||
                              (m_settings.speedrunKeep == 2 && finished && m_speedrun.lastWasPb());
            std::error_code ec;
            if (keep && fs::exists(file, ec)) {
                m_speedrun.attachVideo(toUtf8(file));
                m_ui.notify("Vidéo de la run enregistrée");
            } else {
                fs::remove(file, ec);
            }
        }
    }
    if (const std::string error = m_recorder.takeError(); !error.empty())
        m_ui.notify(error);
}

// --- Emplacements de sauvegarde ---------------------------------------------------------------

std::filesystem::path App::slotFile(int slot, const char* extension) const
{
    return m_gameSaveDir / fromUtf8("slot" + std::to_string(slot + 1) + extension);
}

void App::saveSlot(int slot)
{
    m_pendingSlot = -1;
    if (!m_core)
        return;
    std::error_code ec;
    fs::create_directories(m_gameSaveDir, ec);
    if (!m_core->saveState(slotFile(slot, ".state"))) {
        m_ui.notify("Échec de la sauvegarde d'état");
        return;
    }
    // La miniature : l'image du jeu réduite à 320 pixels de large.
    int w = 0;
    int h = 0;
    std::vector<uint8_t> pixels;
    if (m_video.readGameArea(pixels, w, h) && w > 0 && h > 0) {
        const int tw = std::min(320, w);
        const int th = std::max(1, h * tw / w);
        std::vector<uint8_t> thumb(static_cast<size_t>(tw) * th * 4);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                // (Lignes de bas en haut dans l'image lue : on la retourne.)
                const uint8_t* src = &pixels[(static_cast<size_t>(h - 1 - y * h / th) * w + x * w / tw) * 4];
                uint8_t* dst = &thumb[(static_cast<size_t>(y) * tw + x) * 4];
                dst[0] = src[2]; // BGRA -> RGBA
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 255;
            }
        const std::string file = toUtf8(slotFile(slot, ".bmp"));
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(tw, th, SDL_PIXELFORMAT_RGBA32, thumb.data(), tw * 4)) {
            SDL_SaveBMP(surface, file.c_str());
            SDL_DestroySurface(surface);
        }
        m_covers.forget(file); // (l'ancienne miniature n'est plus la bonne)
    }
    m_ui.notify("Sauvegardé dans l'emplacement " + std::to_string(slot + 1));
}

void App::loadSlot(int slot)
{
    if (!m_core)
        return;
    m_speedrun.markAssisted();
    if (m_core->loadState(slotFile(slot, ".state"))) {
        m_ui.notify("Emplacement " + std::to_string(slot + 1) + " chargé");
        setMenuOpen(false);
    } else {
        m_ui.notify("Cet emplacement est vide");
    }
}

void App::drawSlotsScreen()
{
    using theme::px;
    using theme::Weight;
    // Une grande carte au centre : 8 emplacements (4 x 2), chacun avec sa
    // miniature, sa date, et ses deux boutons.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                      IM_COL32(0, 0, 0, 150));
    const ImVec2 size(std::min(px(980.0f), viewport->Size.x - px(32.0f)), std::min(px(620.0f), viewport->Size.y - px(32.0f)));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(26.0f, 26.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(24.0f));
    ImGui::Begin("##slots", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float inner = ImGui::GetContentRegionAvail().x;
    const ImVec2 top = ImGui::GetCursorPos();
    ui::text("Emplacements de sauvegarde", 22.0f, Weight::ExtraBold);
    ui::text(m_gameTitle, 13.0f, Weight::SemiBold, theme::Muted);
    const ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(top.x + inner - px(120.0f), top.y));
    if (ui::button("Retour", {.style = ui::ButtonStyle::Soft, .icon = draw::Icon::ChevronLeft, .height = 40.0f, .width = 120.0f}) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        m_slotsOpen = false;
    ImGui::SetCursorPos(after);
    ui::space(8.0f);
    constexpr int kSlots = 8;
    const float gap = px(14.0f);
    const float cardW = (inner - gap * 3) / 4;
    // Deux rangées qui tiennent dans la hauteur disponible (petite fenêtre comprise).
    const float cardH = std::min(cardW * 0.62f + px(96.0f), (ImGui::GetContentRegionAvail().y - gap) / 2);
    const float thumbH = std::max(px(30.0f), cardH - px(96.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int i = 0; i < kSlots; ++i) {
        ImGui::PushID(i);
        const ImVec2 p(origin.x + (i % 4) * (cardW + gap), origin.y + (i / 4) * (cardH + gap));
        draw::rect(dl, p, ImVec2(p.x + cardW, p.y + cardH), theme::Ground, px(16.0f));
        const fs::path state = slotFile(i, ".state");
        const long long when = ui::fileTime(state);
        const ImVec2 tmin(p.x + px(8.0f), p.y + px(8.0f));
        const ImVec2 tmax(p.x + cardW - px(8.0f), p.y + px(8.0f) + thumbH);
        if (when == 0 || !m_covers.drawImage(dl, tmin, tmax, toUtf8(slotFile(i, ".bmp")), px(10.0f), 320)) {
            draw::rect(dl, tmin, tmax, theme::Track, px(10.0f));
            const char* label = when == 0 ? "Vide" : "État sans miniature";
            const ImVec2 ls = draw::measure(Weight::SemiBold, 12.0f, label);
            draw::text(dl, ImVec2((tmin.x + tmax.x - ls.x) / 2, (tmin.y + tmax.y - ls.y) / 2), Weight::SemiBold, 12.0f, theme::Muted,
                       label);
        }
        draw::text(dl, ImVec2(p.x + px(12.0f), tmax.y + px(8.0f)), Weight::Bold, 13.0f, theme::Ink,
                   "Emplacement " + std::to_string(i + 1));
        draw::textEllipsis(dl, ImVec2(p.x + px(12.0f), tmax.y + px(27.0f)), Weight::Medium, 11.0f, theme::Muted,
                           when ? ui::formatAgo(when) : std::string("—"), cardW - px(24.0f));
        const float bw = (cardW - px(24.0f) - px(6.0f)) / 2 / theme::scale();
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(12.0f), p.y + cardH - px(44.0f)));
        if (ui::button("Sauver", {.style = ui::ButtonStyle::Primary, .height = 34.0f, .width = bw, .fontSize = 12.0f}))
            m_pendingSlot = i; // (la miniature sera prise sur l'image du jeu, sans le menu)
        ImGui::SameLine(0.0f, px(6.0f));
        if (ui::button("Charger", {.style = ui::ButtonStyle::Outline, .height = 34.0f, .width = bw, .fontSize = 12.0f,
                                   .disabled = when == 0}))
            loadSlot(i);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 2 * cardH + gap));
    ImGui::Dummy(ImVec2(inner, px(1.0f)));
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// --- Frise du retour en arrière -------------------------------------------------------------------

void App::captureRewindThumbnail()
{
    if (m_mode != Mode::Playing || !m_rewindUsable || !m_settings.rewind || m_rewind.frames() == 0)
        return;
    if (m_timeMode != TimeMode::Normal)
        return;
    // Le passé oublié, et (après un retour en arrière) le "futur" abandonné.
    while (!m_rewindThumbs.empty() && m_rewindThumbs.front().index < m_rewind.oldest()) {
        Covers::deleteTexture(m_rewindThumbs.front().texture);
        m_rewindThumbs.pop_front();
    }
    while (!m_rewindThumbs.empty() && m_rewindThumbs.back().index > m_rewind.newest()) {
        Covers::deleteTexture(m_rewindThumbs.back().texture);
        m_rewindThumbs.pop_back();
    }
    // Une miniature par seconde de jeu.
    const double fps = m_core ? std::max(1.0, m_core->avInfo().timing.fps) : 60.0;
    if (!m_rewindThumbs.empty() && static_cast<double>(m_rewind.newest() - m_rewindThumbs.back().index) < fps)
        return;
    int w = 0;
    int h = 0;
    std::vector<uint8_t> pixels;
    if (!m_video.readGameArea(pixels, w, h) || w <= 0 || h <= 0)
        return;
    const int tw = std::min(160, w);
    const int th = std::max(1, h * tw / w);
    std::vector<uint8_t> thumb(static_cast<size_t>(tw) * th * 4);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            const uint8_t* src = &pixels[(static_cast<size_t>(h - 1 - y * h / th) * w + x * w / tw) * 4];
            uint8_t* dst = &thumb[(static_cast<size_t>(y) * tw + x) * 4];
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    m_rewindThumbs.push_back({m_rewind.newest(), Covers::createTexture(thumb.data(), tw, th), tw, th});
    // (Au plus 10 minutes de miniatures : 600 petites textures.)
    while (m_rewindThumbs.size() > 600) {
        Covers::deleteTexture(m_rewindThumbs.front().texture);
        m_rewindThumbs.pop_front();
    }
}

void App::clearRewindThumbnails()
{
    for (const RewindThumb& t : m_rewindThumbs)
        Covers::deleteTexture(t.texture);
    m_rewindThumbs.clear();
}

void App::drawRewindTimeline()
{
    using theme::px;
    using theme::Weight;
    // En bas de l'écran : le passé disponible, de gauche (le plus ancien) à
    // droite (là où le retour en arrière a commencé), avec des miniatures et
    // un repère à l'image affichée.
    const uint64_t oldest = m_rewind.oldest();
    const uint64_t from = std::max(m_rewindFrom, m_rewind.newest());
    const uint64_t now = m_rewind.newest();
    if (from <= oldest)
        return;
    const double fps = m_core ? std::max(1.0, m_core->avInfo().timing.fps) : 60.0;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(px(900.0f), viewport->Size.x - px(40.0f));
    const float thumbH = px(62.0f);
    const ImVec2 min(viewport->Pos.x + (viewport->Size.x - width) / 2, viewport->Pos.y + viewport->Size.y - px(130.0f));
    const ImVec2 max(min.x + width, min.y + px(112.0f));
    draw::rect(dl, min, max, IM_COL32(14, 14, 20, 215), px(18.0f));
    const float trackL = min.x + px(16.0f);
    const float trackR = max.x - px(16.0f);
    auto xOf = [&](uint64_t index) {
        return trackL + (trackR - trackL) * static_cast<float>(static_cast<double>(index - oldest) / static_cast<double>(from - oldest));
    };
    // Les miniatures : au plus 9, réparties sur la frise.
    const int slots = 9;
    const float slotW = (trackR - trackL) / slots;
    for (int s = 0; s < slots; ++s) {
        const uint64_t target = oldest + static_cast<uint64_t>((from - oldest) * (s + 0.5) / slots);
        const RewindThumb* best = nullptr;
        for (const RewindThumb& t : m_rewindThumbs)
            if (t.index >= oldest && t.index <= from &&
                (!best || std::llabs(static_cast<long long>(t.index) - static_cast<long long>(target)) <
                              std::llabs(static_cast<long long>(best->index) - static_cast<long long>(target))))
                best = &t;
        if (!best)
            continue;
        const float w = std::min(slotW - px(6.0f), thumbH * best->width / std::max(1, best->height));
        const ImVec2 a(trackL + s * slotW + (slotW - w) / 2, min.y + px(12.0f));
        const bool future = best->index > now; // déjà "remonté" : estompé
        dl->AddImageRounded(static_cast<ImTextureID>(static_cast<intptr_t>(best->texture)), a, ImVec2(a.x + w, a.y + thumbH),
                            ImVec2(0, 0), ImVec2(1, 1), future ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE, px(6.0f));
    }
    // La piste et le repère.
    const float trackY = min.y + px(12.0f) + thumbH + px(12.0f);
    dl->AddLine(ImVec2(trackL, trackY), ImVec2(trackR, trackY), IM_COL32(255, 255, 255, 50), px(4.0f));
    dl->AddLine(ImVec2(trackL, trackY), ImVec2(xOf(now), trackY), IM_COL32(110, 140, 255, 255), px(4.0f));
    dl->AddCircleFilled(ImVec2(xOf(now), trackY), px(7.0f), IM_COL32_WHITE, 20);
    char label[64];
    std::snprintf(label, sizeof(label), "-%.1f s", static_cast<double>(from - now) / fps);
    const ImVec2 ls = draw::measure(Weight::ExtraBold, 13.0f, label);
    float lx = std::clamp(xOf(now) - ls.x / 2, min.x + px(8.0f), max.x - px(8.0f) - ls.x);
    draw::text(dl, ImVec2(lx, min.y + px(12.0f) + thumbH - ls.y - px(4.0f)), Weight::ExtraBold, 13.0f, IM_COL32_WHITE, label);
}

void App::drawInputDisplay()
{
    using theme::px;
    // Une manette dessinée en bas à gauche de l'image du jeu : chaque bouton
    // s'allume quand il est appuyé (touches du joueur de ce PC).
    const Input::PadState pad = m_input.sampleLocal();
    auto down = [&](int id) { return (pad.buttons >> id) & 1; };
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float* game = m_video.gameRect();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float left = game[2] > 0 ? game[0] : 0.0f;
    const float bottom = game[3] > 0 ? game[1] + game[3] : viewport->Size.y;
    const ImVec2 size(px(210.0f), px(104.0f));
    const ImVec2 min(left + px(16.0f), bottom - px(16.0f) - size.y);
    const ImU32 panel = IM_COL32(14, 14, 20, 200);
    const ImU32 off = IM_COL32(255, 255, 255, 40);
    const ImU32 on = IM_COL32(110, 140, 255, 255);
    draw::rect(dl, min, ImVec2(min.x + size.x, min.y + size.y), panel, px(18.0f));
    // Gâchettes.
    auto shoulder = [&](float x, int id, int id2, const char* label) {
        const ImVec2 a(min.x + x, min.y + px(8.0f));
        const bool pressed = down(id) || down(id2);
        draw::rect(dl, a, ImVec2(a.x + px(46.0f), a.y + px(14.0f)), pressed ? on : off, px(7.0f));
        const ImVec2 ts = draw::measure(theme::Weight::Bold, 9.0f, label);
        draw::text(dl, ImVec2(a.x + (px(46.0f) - ts.x) / 2, a.y + (px(14.0f) - ts.y) / 2), theme::Weight::Bold, 9.0f,
                   IM_COL32(255, 255, 255, 200), label);
    };
    shoulder(px(14.0f), RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_L2, "L");
    shoulder(size.x - px(60.0f), RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_R2, "R");
    // Croix directionnelle.
    const ImVec2 c(min.x + px(46.0f), min.y + px(62.0f));
    const float arm = px(12.0f);
    auto dpad = [&](float dx, float dy, int id) {
        const ImVec2 p(c.x + dx * arm - arm / 2, c.y + dy * arm - arm / 2);
        draw::rect(dl, p, ImVec2(p.x + arm, p.y + arm), down(id) ? on : off, px(3.0f));
    };
    dpad(0, -1, RETRO_DEVICE_ID_JOYPAD_UP);
    dpad(0, 1, RETRO_DEVICE_ID_JOYPAD_DOWN);
    dpad(-1, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    dpad(1, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    draw::rect(dl, ImVec2(c.x - arm / 2, c.y - arm / 2), ImVec2(c.x + arm / 2, c.y + arm / 2), off, 0.0f);
    // Select et Start au centre.
    auto pill = [&](float x, int id) {
        const ImVec2 p(min.x + x, min.y + px(70.0f));
        draw::rect(dl, p, ImVec2(p.x + px(20.0f), p.y + px(8.0f)), down(id) ? on : off, px(4.0f));
    };
    pill(px(84.0f), RETRO_DEVICE_ID_JOYPAD_SELECT);
    pill(px(108.0f), RETRO_DEVICE_ID_JOYPAD_START);
    // Boutons A, B, X, Y (disposition Super Nintendo).
    const ImVec2 f(min.x + size.x - px(44.0f), min.y + px(62.0f));
    auto face = [&](float dx, float dy, int id, const char* label) {
        const ImVec2 p(f.x + dx * px(15.0f), f.y + dy * px(15.0f));
        dl->AddCircleFilled(p, px(9.0f), down(id) ? on : off, 20);
        const ImVec2 ts = draw::measure(theme::Weight::Bold, 9.0f, label);
        draw::text(dl, ImVec2(p.x - ts.x / 2, p.y - ts.y / 2), theme::Weight::Bold, 9.0f, IM_COL32(255, 255, 255, 220), label);
    };
    face(0, -1, RETRO_DEVICE_ID_JOYPAD_X, "X");
    face(-1, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y");
    face(1, 0, RETRO_DEVICE_ID_JOYPAD_A, "A");
    face(0, 1, RETRO_DEVICE_ID_JOYPAD_B, "B");
}

void App::drawSpeedrunOverlay()
{
    using theme::px;
    using theme::Weight;
    speedrun::Category* cat = m_speedrun.category();
    if (!cat)
        return;
    // Un panneau à la LiveSplit, en haut à gauche de l'image du jeu (à
    // droite, les notifications passeraient dessus).
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float* game = m_video.gameRect();
    const float left = game[2] > 0 ? game[0] : 0.0f;
    const float top = game[3] > 0 ? game[1] : 0.0f;
    const float width = px(300.0f);
    const float rowH = px(30.0f);
    const int count = static_cast<int>(cat->segments.size());
    const int current = m_speedrun.state() == speedrun::Speedrun::State::Running ? m_speedrun.currentSplit() : -1;
    // Au plus 8 splits visibles, autour de celui en cours (le dernier toujours affiché).
    constexpr int kVisible = 8;
    int first = 0;
    if (count > kVisible)
        first = std::clamp((current < 0 ? count : current) - kVisible / 2, 0, count - kVisible);
    const int shown = std::min(count, kVisible);
    const float height = px(64.0f) + rowH * static_cast<float>(shown) + px(96.0f);
    const ImVec2 min(left + px(16.0f), top + px(16.0f));
    const ImVec2 max(min.x + width, min.y + height);
    const ImU32 panel = IM_COL32(14, 14, 20, 225);
    const ImU32 white = IM_COL32(245, 245, 250, 255);
    const ImU32 muted = IM_COL32(160, 160, 175, 255);
    const ImU32 ahead = IM_COL32(52, 211, 120, 255);
    const ImU32 behind = IM_COL32(248, 92, 92, 255);
    const ImU32 gold = IM_COL32(250, 200, 60, 255);
    draw::shadow(dl, min, max, px(16.0f), px(18.0f), px(8.0f), 70);
    draw::rect(dl, min, max, panel, px(16.0f));

    // En-tête : le jeu, la catégorie, le nombre d'essais.
    const float pad = px(16.0f);
    draw::textEllipsis(dl, ImVec2(min.x + pad, min.y + px(12.0f)), Weight::ExtraBold, 14.0f, white, m_speedrun.currentTitle(),
                       width - pad * 2 - px(50.0f));
    draw::textEllipsis(dl, ImVec2(min.x + pad, min.y + px(32.0f)), Weight::SemiBold, 12.0f, muted, cat->name,
                       width - pad * 2 - px(50.0f));
    const std::string attempts = "#" + std::to_string(cat->attempts);
    const ImVec2 as = draw::measure(Weight::Bold, 12.0f, attempts);
    draw::text(dl, ImVec2(max.x - pad - as.x, min.y + px(14.0f)), Weight::Bold, 12.0f, muted, attempts);

    // Les splits : nom, écart avec le record, temps.
    const std::vector<long long>& splits = m_speedrun.splits();
    const std::vector<char>& golds = m_speedrun.golds();
    const long long elapsed = m_speedrun.elapsedMs();
    float y = min.y + px(58.0f);
    for (int i = first; i < first + shown; ++i) {
        const speedrun::Segment& seg = cat->segments[static_cast<size_t>(i)];
        if (i == current)
            draw::rect(dl, ImVec2(min.x + px(6.0f), y), ImVec2(max.x - px(6.0f), y + rowH), IM_COL32(80, 110, 255, 70), px(8.0f));
        const float textY = y + (rowH - draw::measure(Weight::SemiBold, 13.0f, "Ag").y) / 2;
        draw::textEllipsis(dl, ImVec2(min.x + pad, textY), Weight::SemiBold, 13.0f, white, seg.name, width * 0.45f);
        const bool done = static_cast<size_t>(i) < splits.size() && splits[static_cast<size_t>(i)] >= 0 &&
                          (current < 0 || i < current);
        std::string timeText;
        ImU32 timeColor = muted;
        std::string deltaText;
        ImU32 deltaColor = muted;
        if (done) {
            const long long t = splits[static_cast<size_t>(i)];
            timeText = speedrun::formatTime(t);
            timeColor = white;
            if (const long long target = m_speedrun.comparison(i); target >= 0) {
                deltaText = speedrun::formatTime(t - target, true, 1);
                deltaColor = t <= target ? ahead : behind;
            }
            if (static_cast<size_t>(i) < golds.size() && golds[static_cast<size_t>(i)])
                deltaColor = gold;
        } else {
            const long long target = m_speedrun.comparison(i);
            timeText = speedrun::formatTime(target); // le temps à battre
            // Split en cours, déjà plus lent que le record : l'écart s'affiche en direct.
            if (i == current && target >= 0 && elapsed > target) {
                deltaText = speedrun::formatTime(elapsed - target, true, 1);
                deltaColor = behind;
            }
        }
        const ImVec2 ts = draw::measure(Weight::Bold, 13.0f, timeText);
        draw::text(dl, ImVec2(max.x - pad - ts.x, textY), Weight::Bold, 13.0f, timeColor, timeText);
        if (!deltaText.empty()) {
            const ImVec2 ds = draw::measure(Weight::Bold, 12.0f, deltaText);
            draw::text(dl, ImVec2(max.x - pad - px(78.0f) - ds.x, textY + px(1.0f)), Weight::Bold, 12.0f, deltaColor, deltaText);
        }
        y += rowH;
    }

    // Le grand chrono : vert en avance sur le record, rouge en retard, or pour un nouveau record.
    dl->AddLine(ImVec2(min.x + pad, y + px(6.0f)), ImVec2(max.x - pad, y + px(6.0f)), IM_COL32(255, 255, 255, 30), px(1.0f));
    ImU32 timerColor = white;
    const auto state = m_speedrun.state();
    if (state == speedrun::Speedrun::State::Ready) {
        timerColor = muted;
    } else if (state == speedrun::Speedrun::State::Finished) {
        timerColor = m_speedrun.lastWasPb() ? gold : white;
    } else if (current >= 0) {
        const long long target = m_speedrun.comparison(current);
        timerColor = target < 0 ? white : (elapsed <= target ? ahead : behind);
    }
    const std::string timer = speedrun::formatTime(elapsed, false, 2);
    const ImVec2 big = draw::measure(Weight::ExtraBold, 34.0f, timer);
    draw::text(dl, ImVec2(max.x - pad - big.x, y + px(12.0f)), Weight::ExtraBold, 34.0f, timerColor, timer);

    // Pied : record, somme des meilleurs, et l'état (enregistrement, run assistée, touches).
    const float footY = y + px(60.0f);
    std::string stats = "PB " + speedrun::formatTime(cat->pb()) + "  ·  SoB " + speedrun::formatTime(cat->sumOfBest());
    if (cat->worldRecord >= 0)
        stats += "  ·  WR " + speedrun::formatTime(cat->worldRecord);
    draw::text(dl, ImVec2(min.x + pad, footY), Weight::SemiBold, 11.0f, muted, stats);
    std::string status;
    ImU32 statusColor = muted;
    if (m_speedrun.assisted()) {
        status = "Run assistée (pas de record)";
        statusColor = gold;
    } else if (m_recorder.recording()) {
        status = "REC";
        statusColor = behind;
        dl->AddCircleFilled(ImVec2(min.x + pad + px(4.0f), footY + px(22.0f)), px(4.0f), behind, 16);
    } else if (state == speedrun::Speedrun::State::Ready) {
        status = "F5 départ · F9 remise à zéro";
    } else if (state == speedrun::Speedrun::State::Finished) {
        status = "F9 : nouvelle tentative";
    } else {
        status = "F5 split · F6 annuler · F7 passer · F9 reset";
    }
    draw::text(dl, ImVec2(min.x + pad + (status == "REC" ? px(14.0f) : 0.0f), footY + px(16.0f)), Weight::Bold, 11.0f,
               statusColor, status);
}

void App::drawTimeBadge()
{
    if (m_timeMode == TimeMode::Normal)
        return;
    std::string label;
    switch (m_timeMode) {
    case TimeMode::Fast: label = "Avance rapide ×" + std::to_string(std::clamp(m_settings.fastForwardSpeed, 2, 8)); break;
    case TimeMode::Rewinding: label = "Retour en arrière"; break;
    case TimeMode::RewindEnd: label = "Début du retour en arrière"; break;
    default: break;
    }
    // En haut au centre, comme le badge "En ligne".
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ts = draw::measure(theme::Weight::Bold, 14.0f, label);
    const float w = ts.x + theme::px(32.0f);
    const ImVec2 min(ImGui::GetMainViewport()->Size.x / 2 - w / 2, theme::px(16.0f));
    draw::rect(dl, min, ImVec2(min.x + w, min.y + theme::px(34.0f)), theme::withAlpha(theme::Ink, 200), theme::px(17.0f));
    draw::text(dl, ImVec2(min.x + theme::px(16.0f), min.y + (theme::px(34.0f) - ts.y) / 2), theme::Weight::Bold, 14.0f,
               theme::Surface, label);
}

void App::saveState()
{
    m_ui.notify(m_core->saveState(m_stateFile) ? "État sauvegardé" : "Échec de la sauvegarde d'état");
}

void App::loadState()
{
    m_speedrun.markAssisted(); // (une run avec un état chargé n'est plus "d'une traite")
    m_ui.notify(m_core->loadState(m_stateFile) ? "État chargé" : "Aucun état à charger");
}

void App::takeScreenshot()
{
    // Dans le dossier de sauvegarde du jeu, avec ses parties.
    const fs::path file = m_gameSaveDir / fromUtf8(m_gameTitle + "-" + std::to_string(now()) + ".bmp");
    m_video.requestScreenshot(file);
    m_ui.notify("Capture d'écran enregistrée");
}
