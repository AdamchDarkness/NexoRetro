#pragma once
// ExternalInstaller : installe en un clic un émulateur externe (RPCS3, xemu,
// Xenia, shadPS4) depuis sa page GitHub OFFICIELLE, dans emulators/<console>/.
//
// Ce qui est téléchargé : uniquement l'émulateur, un logiciel libre publié
// par ses auteurs. Jamais de BIOS, de firmware, de clés ni de jeux : ceux-là
// viennent de la console de l'utilisateur (voir Paramètres > Émulateurs
// externes). Pas d'installation automatique pour la Switch : ses émulateurs
// ont été arrêtés après les procès de Nintendo (2024).
//
// Le travail (téléchargement de 30 à 40 Mo, décompression) se fait dans un
// thread : l'interface lit l'avancement à chaque image.

#include "net/Http.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class ExternalInstaller {
public:
    // D'où vient l'émulateur d'une console.
    struct Source {
        const char* systemId;
        const char* repo;       // "RPCS3/rpcs3-binaries-win"
        const char* must[2];    // le nom du fichier doit contenir ces mots...
        const char* mustNot[2]; // ... et pas ceux-là
        const char* extension;  // ".zip" ou ".7z"
        const char* exe;        // le programme à trouver dans l'archive
    };
    static const Source* sourceFor(const std::string& systemId);

    explicit ExternalInstaller(std::filesystem::path emulatorsDir);
    ~ExternalInstaller();
    ExternalInstaller(const ExternalInstaller&) = delete;
    ExternalInstaller& operator=(const ExternalInstaller&) = delete;

    // Lance l'installation (false si une autre est en cours).
    bool start(const std::string& systemId);
    // Même chose pour un core libretro manquant ("pcsx2_libretro.dll"),
    // depuis le serveur officiel de libretro, vers le dossier cores/.
    // (Dans le résultat, systemId vaut alors "core:pcsx2_libretro.dll".)
    bool startCore(const std::string& coreFile, const std::filesystem::path& coresDir);
    // Les fichiers système LIBRES d'un core, que libretro distribue (pas des
    // BIOS) : "Dolphin" ou "PPSSPP" -> <nom>.zip, décompressé dans bios/.
    // (Dans le résultat, systemId vaut alors "files:Dolphin".)
    bool startSystemFiles(const std::string& name, const std::filesystem::path& biosDir);
    bool busy() const { return m_busy; }
    std::string busySystem() const;
    // 0 à 1 pendant le téléchargement, -1 si on ne sait pas (décompression...).
    float progress() const;
    std::string stepText() const;

    struct Result {
        std::string systemId;
        bool ok = false;
        std::string message;
        std::filesystem::path exe;
    };
    std::optional<Result> takeResult();

private:
    void run(const Source& source);
    // Télécharge un .zip et le décompresse dans dest ; check : le fichier qui
    // doit exister après (relatif à dest) ; extracted : le nom qu'il porte
    // dans l'archive, s'il est différent (il est alors renommé en check).
    void runZip(const std::string& id, const std::string& url, const std::filesystem::path& dest,
                const std::filesystem::path& check, const std::filesystem::path& extracted = {});
    bool begin(const std::string& id); // prépare une installation (false si occupé)
    void setStep(std::string text);
    void finish(Result result);

    std::filesystem::path m_dir;
    std::thread m_thread;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_extracting{false};
    net::HttpClient::Progress m_progress;
    mutable std::mutex m_mutex; // protège ce qui suit
    std::string m_system;
    std::string m_step;
    std::optional<Result> m_result;
};
