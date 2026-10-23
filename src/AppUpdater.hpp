#pragma once
// AppUpdater : garde Nexo Retro lui-même à jour, depuis ses versions
// publiées sur GitHub (les "releases" du dépôt).
//
// 1. Vérifier : on demande à l'API de GitHub la dernière version publiée
//    (un petit texte JSON : numéro, notes, fichiers joints). Si son numéro
//    est plus grand que le nôtre (NEXO_VERSION, tiré de CMakeLists.txt),
//    une mise à jour existe.
// 2. Télécharger : l'installateur (NexoRetro-Setup-<version>.exe) va dans le
//    dossier temporaire de Windows, puis on calcule son empreinte SHA-256 et
//    on la compare à celle publiée dans SHA256SUMS.txt. Un seul octet
//    différent (téléchargement abîmé, fichier remplacé) : on refuse.
// 3. Installer : on lance l'installateur en mode silencieux, puis
//    l'application se ferme normalement (sauvegardes comprises).
//    L'installateur remplace le programme, sans toucher aux données, et
//    relance Nexo Retro tout seul.
//
// L'installation automatique n'existe que pour la copie INSTALLÉE (un
// fichier installed.txt, posé par l'installateur, à côté de NexoRetro.exe).
// Une copie portable (dossier .zip) ou de développement propose seulement
// d'ouvrir la page de téléchargement.
//
// Comme CoreUpdater, le travail réseau se fait dans un thread : l'interface
// lit l'état à chaque image, sans jamais attendre.

#include "net/Http.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class AppUpdater {
public:
    // La version de ce programme ("0.2.0").
    static const char* currentVersion();
    // Compare deux numéros de version "sémantiques" (MAJEUR.MINEUR.CORRECTIF,
    // avec ou sans "v" devant). Renvoie <0, 0 ou >0, comme strcmp.
    // Chaque nombre compte en entier : 0.10.0 est PLUS RÉCENT que 0.9.0
    // (une comparaison de textes dirait l'inverse).
    static int compareVersions(std::string_view a, std::string_view b);

    // exeDir : le dossier de NexoRetro.exe (on y cherche installed.txt).
    explicit AppUpdater(std::filesystem::path exeDir);
    ~AppUpdater();
    AppUpdater(const AppUpdater&) = delete;
    AppUpdater& operator=(const AppUpdater&) = delete;

    enum class State {
        Idle,        // rien de demandé
        Checking,    // question posée à GitHub
        UpToDate,    // on a déjà la dernière version
        Available,   // une version plus récente existe
        Downloading, // l'installateur arrive
        Ready,       // téléchargé et vérifié : prêt à installer
        Error        // vérification ou téléchargement impossible
    };

    // Demande la dernière version (en arrière-plan). automatic : lancée
    // toute seule au démarrage (pas par le bouton).
    void check(bool automatic);
    // Télécharge et vérifie l'installateur de la version trouvée.
    void download();
    // À chaque image : récupère le résultat du thread.
    void update();

    State state() const { return m_state; }
    bool installedCopy() const { return m_installed; } // false : portable ou développement
    bool automatic() const { return m_automatic; }     // la dernière vérification était automatique
    // La version trouvée ("0.3.0", sans le "v"), ses notes, sa page web.
    const std::string& latestVersion() const { return m_latest; }
    const std::string& releaseName() const { return m_name; }
    const std::string& notes() const { return m_notes; }
    const std::string& pageUrl() const { return m_page; }
    const std::string& error() const { return m_error; }
    // Avancement du téléchargement, 0 à 1 (-1 si la taille est inconnue).
    float progress() const;

    // Une vérification vient de se terminer (une seule fois par vérification) :
    // pour noter sa date dans les réglages.
    bool takeCheckFinished();

    // Prépare l'installation : l'installateur sera lancé à la toute fin du
    // programme (runPendingInstaller, appelée par main()), une fois l'appli
    // fermée et tout enregistré. Renvoie false si rien n'est prêt.
    bool scheduleInstall();
    bool installScheduled() const { return !s_pendingInstaller.empty(); }
    // Lance l'installateur préparé, s'il y en a un (en mode silencieux).
    static void runPendingInstaller();

private:
    struct Found {
        bool ok = false;
        int status = 0;         // code HTTP (404 : aucune version publiée, ou dépôt privé)
        std::string error;
        std::string version, name, notes, page;
        std::string setupUrl;   // NexoRetro-Setup-<version>.exe
        std::string setupName;
        std::string sumsUrl;    // SHA256SUMS.txt
    };
    struct Downloaded {
        bool ok = false;
        std::string error;
        std::filesystem::path file;
        std::string hash; // son empreinte SHA-256 (vérifiée)
    };
    void join();

    std::filesystem::path m_exeDir;
    bool m_installed = false;
    State m_state = State::Idle;
    bool m_automatic = false;
    bool m_checkFinished = false;
    std::string m_latest, m_name, m_notes, m_page, m_error;
    std::string m_setupUrl, m_setupName, m_sumsUrl;
    std::filesystem::path m_setupFile; // l'installateur vérifié...
    std::string m_setupHash;           // ... et son empreinte

    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
    net::HttpClient::Progress m_progress;
    std::mutex m_mutex; // protège les deux résultats ci-dessous
    std::optional<Found> m_found;
    std::optional<Downloaded> m_downloaded;

    // [C++] "inline static" : une variable partagée par toute la classe
    // (comme une globale, mais rangée dans AppUpdater), définie ici même.
    inline static std::filesystem::path s_pendingInstaller;
};
