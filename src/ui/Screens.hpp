#pragma once
// Screens : les écrans de l'application et ce qu'ils partagent.
//
// Chaque écran est une classe avec une méthode draw(), appelée à chaque
// image tant que l'écran est affiché. Son code se trouve dans le fichier du
// même nom (HomeScreen.cpp, LibraryScreen.cpp...).
//
// Les écrans ne lancent rien eux-mêmes : ils passent par le UiContext, qui
// donne accès aux données (bibliothèque, réglages...) et aux actions de
// l'application (jouer, ouvrir un fichier, afficher un message...).

#include "../Bios.hpp"
#include "../Cheats.hpp"
#include "../Core.hpp"
#include "../AppUpdater.hpp"
#include "../CoreUpdater.hpp"
#include "../Discord.hpp"
#include "../ExternalInstaller.hpp"
#include "../FileDialogs.hpp"
#include "../Input.hpp"
#include "../Library.hpp"
#include "../LibraryTools.hpp"
#include "../PortableCopy.hpp"
#include "../Speedrun.hpp"
#include "../SpeedrunCom.hpp"
#include "../Settings.hpp"
#include "../UserData.hpp"
#include "../VoiceChat.hpp"
#include "../net/Netplay.hpp"
#include "../net/Session.hpp"
#include "../scrape/Scraper.hpp"
#include "Covers.hpp"

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ui {

enum class Screen { Home, Library, Game, AddGames, Bios, Friends, Settings, Speedrun };

// Où l'on se trouve dans l'application.
struct Navigation {
    Screen screen = Screen::Home;
    std::filesystem::path game;     // jeu affiché sur la fiche
    std::string librarySystem;      // filtre de la bibliothèque à appliquer
    bool focusSearch = false;       // placer le curseur dans la recherche
    int settingsTab = -1;           // onglet des Paramètres à ouvrir (-1 = inchangé)
    int settingsDevice = -2;        // Manettes : appareil à sélectionner (-1 = clavier, -2 = inchangé)
    bool openCoverPicker = false;   // fiche du jeu : ouvrir "Changer la jaquette" (tests)
    bool openDelete = false;        // fiche du jeu : ouvrir "Supprimer" (tests)
    bool openSave = false;          // ... "Sauvegarde du jeu" (tests)
    bool openCheats = false;        // ... "Codes de triche", onglet en ligne (tests)
    bool openDuplicates = false;    // Paramètres : ouvrir "Doublons et versions" (tests)
    std::string externalChoosing;   // console dont on choisit l'émulateur externe
    bool bigPicture = false;        // mode Big Picture (plein écran, à la manette)
    bool speedrunLaunch = false;    // le prochain jeu lancé l'est depuis l'écran Speedrun (chrono affiché)
    bool openFilters = false;       // bibliothèque : ouvrir la fenêtre des filtres (tests)
    bool openViews = false;         // bibliothèque : ouvrir le menu « Vue » (tests)

    void go(Screen s) { screen = s; }
    void openGame(const Game& g) { screen = Screen::Game; game = g.path; }
};

// Une ligne du chat du salon.
struct ChatLine {
    std::string author; // vide = message du système ("Lou est connecté")
    std::string text;
    bool mine = false;
};

// Ce que l'interface doit savoir du salon, en plus de la Session.
struct OnlineState {
    std::vector<ChatLine> chat;
    std::filesystem::path picking; // hôte : jeu choisi, empreinte en cours de calcul
    std::filesystem::path match;   // le fichier de CE PC qui correspond au jeu du salon
    float hashProgress = 0.0f;     // calcul d'empreinte en cours (0 à 1)
    // Hôte : point de départ de la partie en ligne. 0 = nouvelle partie,
    // 1 = sa sauvegarde du jeu (.srm), 2 = sauvegarde d'état auto, 3 = rapide (F2).
    int startFrom = 0;
    // Câble link : le jeu choisi par CE joueur (vide = celui du même nom que le jeu du salon).
    std::filesystem::path linkChoice;
    // Soirée jeu (hôte) : la liste préparée, et le jeu en cours (-1 = pas commencée).
    std::vector<std::filesystem::path> night;
    int nightIndex = -1;
};

const char* statusLabel(net::GameStatus status);

// Tout ce dont un écran a besoin. [C++] std::function stocke "quelque chose
// qu'on peut appeler" (ici des lambdas fournies par App).
struct UiContext {
    Library& library;
    UserData& userData;
    Settings& settings;
    BiosManager& bios;
    Covers& covers;
    Input& input;
    Navigation& nav;
    net::Session& session;
    net::RelayServer& relayServer;
    OnlineState& online;
    scrape::Scraper& scraper;
    ExternalInstaller& installer;
    VoiceChat& voice;
    CoreUpdater& coreUpdater;
    DiscordPresence& discord;
    PortableCopy& portableCopy;
    speedrun::Speedrun& speedrun;
    std::filesystem::path dataDir;

    std::function<void(const Game&, const std::filesystem::path& state)> play;
    std::function<void(std::string)> notify;
    std::function<void(DialogPurpose)> openDialog;
    std::function<void()> rescan;
    std::function<void()> settingsChanged;
    std::function<void(const Game&)> chooseSalonGame; // hôte : jeu du salon
    std::function<void()> startNetplay;               // hôte : lancer la partie en ligne
    std::function<void(bool)> setLinkMode;            // hôte : mode câble link oui/non
    // Retirer un jeu de la bibliothèque ; deleteFiles : ses fichiers vont
    // aussi à la Corbeille.
    std::function<void(const Game&, bool deleteFiles)> removeGame;
    // Charge un core un instant pour lire la liste de ses options.
    std::function<std::vector<CoreOption>(const std::filesystem::path& dll)> inspectCore;
    // Entrer dans le mode Big Picture (plein écran) ou en sortir.
    std::function<void(bool)> setBigPicture;
    // Mise à jour de Nexo Retro (voir AppUpdater.hpp). Renseignés par App
    // juste après la construction : d'où les valeurs par défaut.
    AppUpdater* appUpdater = nullptr;
    // "Installer maintenant" : télécharge si besoin, puis ferme l'appli et
    // lance l'installateur (jamais pendant une partie en ligne).
    std::function<void()> installUpdate;

    std::filesystem::path dir(const char* name) const { return dataDir / name; }
};

// Fichiers de sauvegarde d'état d'un jeu.
std::filesystem::path quickStateFile(const UiContext& ctx, const Game& game);
std::filesystem::path autoStateFile(const UiContext& ctx, const Game& game);
// Le dossier des sauvegardes d'un jeu : saves/<nom>/.
std::filesystem::path gameSaveDir(const UiContext& ctx, const Game& game);
// La sauvegarde du jeu lui-même (la "pile" de la cartouche) : saves/<nom>/<nom>.srm.
std::filesystem::path gameSaveFile(const UiContext& ctx, const Game& game);
// Les codes de triche du jeu : cheats/<nom>.txt.
std::filesystem::path cheatsFile(const UiContext& ctx, const Game& game);

// Ouvre un dossier dans l'explorateur Windows.
void openFolder(const std::filesystem::path& dir);

// --- Mise en forme -----------------------------------------------------
std::string formatAgo(long long timestamp);   // "Il y a 2 h", "Hier"...
std::string formatDuration(long long seconds); // "41 h", "35 min"
std::string formatSize(std::uintmax_t bytes);  // "1,2 Go"
long long fileTime(const std::filesystem::path& file); // 0 si absent

// --- Morceaux d'écran communs -------------------------------------------
// Grand titre de page avec sous-titre.
void pageTitle(const char* title, const std::string& subtitle);
// Le logo de Nexo Retro (assets/logo/nexo-256.png) et son nom : « Nexo »
// en gras, « Retro » en bleu. brand() renvoie la largeur dessinée.
std::string logoFile();
float brand(const UiContext& ctx, ImDrawList* dl, ImVec2 topLeft, float logoSize, float textSize, ImU32 ink);
// Titre de section avec un lien à droite ; renvoie true si le lien est cliqué.
bool sectionHeader(const char* title, const char* link, float width);

struct TileResult {
    bool clicked = false;
    bool doubleClicked = false;
};
// Le nom d'une vue de la bibliothèque (Settings::libraryView) : « Grille de
// jaquettes », « Boîtes 3D »...
const char* libraryViewName(int view);
constexpr int kLibraryViewCount = 6;

// Jaquette cliquable avec titre et sous-titre dessous.
TileResult coverTile(const UiContext& ctx, const Game& game, float width, float height, const std::string& subtitle,
                     float titleSize);

// --- Les écrans ----------------------------------------------------------

// Soirée jeu : l'hôte passe au jeu suivant de sa liste (choisi pour tout le
// salon, annoncé dans la discussion). Appelée aussi à la fin de chaque partie.
void advanceGameNight(UiContext& ctx);

// Lance la copie portable de Nexo Retro dans ce dossier (clé USB...).
void startPortableCopy(UiContext& ctx, const std::filesystem::path& dest);

class HomeScreen {
public:
    void draw(UiContext& ctx);
};

class LibraryScreen {
public:
    void draw(UiContext& ctx);

private:
    void drawGrid(UiContext& ctx, const std::vector<const Game*>& games);
    void drawList(UiContext& ctx, const std::vector<const Game*>& games);
    void drawCarousel(UiContext& ctx, const std::vector<const Game*>& games);
    void drawViewSwitch(UiContext& ctx);
    void drawFiltersPopup(const std::set<std::string>& genres, const std::set<std::string>& publishers,
                          const std::set<int>& decades, ImVec2 topRight);
    bool removablePill(const std::string& label);
    void resetFilters();

    std::string m_filter = "all"; // "all", "fav", "recent" ou l'id d'une console
    char m_search[128] = "";
    int m_sort = 0;
    // Filtres (vide / 0 = tous).
    std::string m_genre;
    std::string m_publisher;
    int m_decade = 0;  // 1990 = années 90
    int m_players = 0; // au moins ce nombre de joueurs
    // Carrousel : le jeu choisi, et la position affichée (qui glisse vers lui).
    int m_carouselIndex = 0;
    float m_carouselPos = 0.0f;
};

class GameScreen {
public:
    void draw(UiContext& ctx);

private:
    void drawCoverPicker(UiContext& ctx, const Game& game);
    void drawRename(UiContext& ctx, const Game& game);
    void drawDelete(UiContext& ctx, const Game& game);
    void drawSaveManager(UiContext& ctx, const Game& game); // la sauvegarde du jeu (.srm)
    void drawCheats(UiContext& ctx, const Game& game);      // les codes de triche
    std::vector<Cheat>& cheatList(UiContext& ctx, const Game& game);
    bool m_openSave = false;
    bool m_openCheats = false;
    int m_cheatTab = 0;
    bool m_cheatSearchStarted = false;
    char m_cheatName[96] = "";
    char m_cheatCode[256] = "";
    char m_cheatFilter[64] = "";
    std::filesystem::path m_cheatsOf; // jeu dont m_cheats est la liste
    std::vector<Cheat> m_cheats;
    void drawOnlineCovers(UiContext& ctx, const Game& game, float width);
    bool m_openDelete = false;
    bool m_deleteFiles = false;      // aussi le fichier (vers la Corbeille)
    bool m_removeRequested = false;
    bool m_openPicker = false;
    int m_pickerTab = 0;
    char m_coverQuery[128] = "";  // recherche de jaquettes en ligne
    bool m_coverSearchStarted = false;
    bool m_openRename = false;
    bool m_focusRename = false;
    char m_renameBuffer[128] = "";
};

class AddGamesScreen {
public:
    void draw(UiContext& ctx);
    // Fichiers ou dossiers déposés / choisis.
    void import(UiContext& ctx, const std::vector<std::filesystem::path>& paths);

private:
    bool importPack(UiContext& ctx, const std::filesystem::path& archive); // .zip de plusieurs jeux
    struct ImportItem {
        enum class Status { Added, Ambiguous, Unsupported, Folder };
        std::filesystem::path path;
        std::string title;
        Status status = Status::Added;
        const System* system = nullptr;
        std::vector<const System*> choices;
        int choice = 0;
    };
    std::vector<ImportItem> m_items;
    char m_networkPath[260] = ""; // \\NAS\Jeux, en cours de saisie
};

class BiosScreen {
public:
    void draw(UiContext& ctx);
    void import(UiContext& ctx, const std::vector<std::filesystem::path>& paths);
};

class FriendsScreen {
public:
    void draw(UiContext& ctx);

private:
    void drawSalon(UiContext& ctx, float width, float height);
    void drawStart(UiContext& ctx, float width);   // créer / rejoindre
    void drawMembers(UiContext& ctx, float width);
    void drawGame(UiContext& ctx, float width, float height);
    void drawGamePicker(UiContext& ctx);
    void drawNight(UiContext& ctx, float width, float height); // soirée jeu (hôte)
    void drawChat(UiContext& ctx, float width, float height);
    void drawHelp(float width);
    char m_salonName[48] = "";
    char m_hostPassword[48] = "";
    char m_code[192] = "";         // code du salon à rejoindre (un code par relais est plus long)
    char m_relayCode[160] = "";    // code du relais d'un ami (héberger sans montrer son adresse)
    bool m_useRelay = false;
    char m_joinPassword[48] = "";
    char m_answer[128] = "";       // code de réponse (côté hôte)
    char m_message[256] = "";      // message de chat en cours d'écriture
    char m_pickerSearch[64] = "";
    bool m_openPicker = false;
    bool m_pickOwn = false;        // câble link : choisir SON jeu (et non celui du salon)
    bool m_pickNight = false;      // soirée jeu : composer la liste (plusieurs jeux)
    bool m_showFullCode = false;   // hôte : afficher aussi le code complet
    size_t m_chatSeen = 0;         // pour défiler vers le bas à chaque nouveau message
};

class SettingsScreen {
public:
    void draw(UiContext& ctx);

private:
    void drawControls(UiContext& ctx, float width); // réassignation des touches
    void drawVoice(UiContext& ctx, float width, bool& changed);
    void drawDiscord(UiContext& ctx, float width, bool& changed);
    void drawCoreOptions(UiContext& ctx, float width);
    void drawExternal(UiContext& ctx, float width); // émulateurs externes
    void drawCores(UiContext& ctx, float width);    // cores libretro : installer, mettre à jour
    void drawUpdates(UiContext& ctx, float width);  // Nexo Retro lui-même : nouvelle version
    void drawAppearance(UiContext& ctx, float width, bool& changed); // thèmes, fond, police, sons
    void drawDuplicates(UiContext& ctx);                            // fenêtre "Doublons et versions"
    void drawParental(UiContext& ctx, float width, bool& changed);  // contrôle parental
    char m_pin[16] = "";
    bool m_parentalUnlocked = false; // code parent donné (jusqu'à "Verrouiller" ou la fermeture)
    std::vector<library_tools::Duplicates> m_duplicates;
    bool m_openDuplicates = false;
    bool m_colorPending = false; // une couleur réglée à la souris, à enregistrer en lâchant
    std::deque<std::string> m_coreQueue;            // cores à installer l'un après l'autre
    std::map<std::string, std::string> m_argsEdit;  // arguments en cours de saisie
    int m_tab = 0;
    // Options des cores : le core choisi, ses options et nos valeurs.
    int m_coreChoice = 0;
    std::string m_inspectedCore;           // DLL dont les options sont affichées
    int m_inspectWait = 0;                 // images à attendre avant de charger le core
    std::vector<CoreOption> m_coreOptions;
    std::map<std::string, std::string> m_optionValues; // contenu de core-options.txt
    int m_device = -1; // -1 = clavier, sinon indice de la manette
    char m_nickname[48] = "";
    bool m_waitingVoiceKey = false; // "Touche pour parler" : en attente d'une touche
    char m_discordId[32] = "";
};

// Speedrun : catégories, splits, records, historique et vidéos des runs.
class SpeedrunScreen {
public:
    void draw(UiContext& ctx);
    // Fichiers LiveSplit (.lss) choisis dans les fenêtres de Windows.
    void importSplits(UiContext& ctx, const std::filesystem::path& file);
    void exportSplits(UiContext& ctx, const std::filesystem::path& file);

private:
    void drawWelcome(UiContext& ctx, float width);
    void drawGame(UiContext& ctx, const Game& game, float width);
    void drawPicker(UiContext& ctx);
    void drawCategoryPopup(UiContext& ctx, speedrun::GameRuns& runs);
    void drawOfficialPopup(UiContext& ctx, speedrun::GameRuns& runs, const Game& game);
    SpeedrunCom m_official;                      // recherche sur speedrun.com
    std::optional<SpeedrunCom::Result> m_officialResult;
    bool m_openOfficial = false;
    std::filesystem::path m_game; // jeu affiché
    bool m_openPicker = false;
    bool m_openCategory = false;
    bool m_confirmCounter = false; // "Remettre à 0" : second clic pour confirmer
    int m_editSplit = -1;          // split en cours de renommage
    char m_splitName[64] = "";
    char m_newSplit[64] = "";
    char m_categoryName[48] = "";
    char m_search[64] = "";
};

// Big Picture : plein écran, sombre, grandes jaquettes, tout à la manette.
class BigPictureScreen {
public:
    void draw(UiContext& ctx);

private:
    int m_category = 0;     // Tous, Favoris, Récents, puis les consoles
    int m_index = 0;        // le jeu choisi
    float m_scroll = 0.0f;  // la position affichée, qui glisse vers m_index
};

// L'enveloppe de l'application : barre du haut + écran courant.
class Shell {
public:
    void draw(UiContext& ctx);
    void onDialogResult(UiContext& ctx, const DialogResult& result);
    void onDrop(UiContext& ctx, const std::vector<std::filesystem::path>& paths);

private:
    void drawTopBar(UiContext& ctx);

    HomeScreen m_home;
    LibraryScreen m_library;
    GameScreen m_game;
    AddGamesScreen m_addGames;
    BiosScreen m_bios;
    FriendsScreen m_friends;
    SettingsScreen m_settings;
    SpeedrunScreen m_speedrun;
    BigPictureScreen m_bigPicture;
};

} // namespace ui
