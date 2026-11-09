#pragma once
// Achievements : les succès RetroAchievements ("Battre le premier boss sans
// être touché : 10 points").
//
// RetroAchievements.org est un site communautaire et gratuit : ses membres ont
// écrit des milliers de succès pour les jeux rétro. Chaque succès est une
// condition sur la mémoire de la console ("l'octet 0x7E0DBE passe de 3 à 4").
// La bibliothèque officielle rcheevos vérifie ces conditions à chaque image ;
// nous lui donnons la mémoire du jeu et transportons ses échanges avec le site.
//
//   1. Connexion : nom + mot de passe, une seule fois. Le site répond par un
//      "jeton" que l'on garde (settings.ini) ; le mot de passe, lui, n'est
//      jamais enregistré.
//   2. Au lancement d'un jeu : rcheevos calcule son empreinte (MD5 du fichier)
//      et demande au site les succès de CE fichier précis.
//   3. À chaque image : doFrame() ; un succès atteint est envoyé au site et
//      annoncé à l'écran.
//
// Mode Hardcore : les succès comptent double au classement, mais sans
// sauvegarde d'état, retour en arrière, ralenti ni code de triche (App les
// bloque, voir blocksAssists()).
//
// Les requêtes HTTP partent d'un fil d'exécution à part ; leurs réponses sont
// rendues à rcheevos dans update(), sur le fil principal : rcheevos, lui, ne
// voit jamais qu'un seul fil.

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Core;
struct rc_client_t;
struct rc_client_event_t;
struct rc_api_request_t;
struct rc_api_server_response_t;

class Achievements {
public:
    enum class Login { LoggedOut, LoggingIn, LoggedIn, Failed };

    // Un message à afficher pendant le jeu.
    struct Notice {
        enum class Kind { Unlocked, Info, Mastered, Error };
        Kind kind = Kind::Info;
        std::string title;  // "Succès débloqué" ou le nom du succès
        std::string text;   // la description
        std::string badge;  // image du succès, sur le disque (peut arriver un peu après ; vide si aucune)
        int points = 0;
    };

    // Un succès du jeu en cours (liste du menu pause).
    struct Item {
        std::string title;
        std::string description;
        std::string badge;      // image sur le disque (débloqué ou grisée selon l'état)
        std::string progress;   // "12/50" pour un succès mesuré, sinon vide
        float percent = 0.0f;   // avancement, 0 à 100
        int points = 0;
        bool unlocked = false;
        bool unsupported = false; // condition que cette version de rcheevos ne sait pas lire
    };

    // Le jeu en cours, vu par RetroAchievements.
    struct GameSummary {
        bool loaded = false;    // le site connaît ce jeu
        std::string title;
        int total = 0;          // succès officiels
        int unlocked = 0;
        int points = 0;
        int pointsUnlocked = 0;
    };

    Achievements();
    ~Achievements();
    Achievements(const Achievements&) = delete;
    Achievements& operator=(const Achievements&) = delete;

    // --- Compte -----------------------------------------------------------
    void loginWithPassword(const std::string& user, const std::string& password);
    void loginWithToken(const std::string& user, const std::string& token);
    void logout();
    Login login() const { return m_login; }
    const std::string& loginError() const { return m_loginError; }
    std::string userName() const;
    int score() const;
    // Après une connexion par mot de passe : le jeton à enregistrer (une fois).
    bool takeNewToken(std::string& user, std::string& token);

    // --- Réglages ---------------------------------------------------------
    // Où ranger les images des succès téléchargées (<données>/achievements).
    void setBadgeDir(std::filesystem::path dir) { m_badgeDir = std::move(dir); }
    void setEnabled(bool on) { m_enabled = on; }
    // Activer le Hardcore en cours de partie relance le jeu (takeResetRequest).
    void setHardcore(bool on);
    bool hardcore() const;
    // Faut-il bloquer sauvegardes d'état, retour en arrière, ralenti et triche ?
    bool blocksAssists() const { return hardcore() && active(); }

    // --- Partie -----------------------------------------------------------
    // Juste après le lancement réussi du jeu (le core a déjà sa mémoire).
    void loadGame(Core& core, const std::string& systemId, const std::filesystem::path& romPath);
    void unloadGame();
    bool active() const; // un jeu reconnu par le site, compte connecté
    void doFrame();      // après chaque image émulée
    void update();       // à chaque tour de boucle (réponses du site, jeu en pause...)
    void reset();        // le jeu a été relancé (bouton Reset)
    void stateLoaded();  // une sauvegarde d'état vient d'être chargée
    bool takeResetRequest();

    GameSummary summary() const;
    std::vector<Item> items() const;
    std::string richPresence() const; // "Monde 1-2, 3 vies" (vide si le jeu n'en a pas)
    std::vector<Notice> takeNotices();

private:
    using ServerCallback = void (*)(const rc_api_server_response_t*, void*);
    struct Request {
        std::string url, post, contentType;
        std::filesystem::path saveTo; // image à télécharger (sans callback)
        ServerCallback callback = nullptr;
        void* data = nullptr;
    };
    struct Response {
        int status = 0;
        std::string body;
        ServerCallback callback = nullptr;
        void* data = nullptr;
    };

    // Appelées par rcheevos (fonctions C : elles retrouvent l'objet par s_instance).
    static uint32_t readMemory(uint32_t address, uint8_t* buffer, uint32_t size, rc_client_t* client);
    static void serverCall(const rc_api_request_t* request, ServerCallback callback, void* data, rc_client_t* client);
    static void onEvent(const rc_client_event_t* event, rc_client_t* client);
    void worker();
    // L'image d'un succès sur le disque ; téléchargée en arrière-plan si besoin.
    std::string badgeFile(const char* url) const;
    void notice(Notice n);

    rc_client_t* m_client = nullptr;
    void* m_memory = nullptr; // rc_libretro_memory_regions_t (voir le .cpp)
    std::string m_userAgent;
    std::filesystem::path m_badgeDir;
    mutable std::vector<std::string> m_badgesAsked; // déjà demandées (pas deux fois)
    Login m_login = Login::LoggedOut;
    std::string m_loginError;
    std::string m_newUser, m_newToken;
    bool m_enabled = true;
    bool m_resetRequested = false;
    bool m_gameLoading = false;
    bool m_framed = false; // doFrame() appelée depuis le dernier update()
    std::vector<Notice> m_notices;

    // Le fil des requêtes HTTP.
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Request> m_requests;
    std::deque<Response> m_responses;
    bool m_stop = false;
};
