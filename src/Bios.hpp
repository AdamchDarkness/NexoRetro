#pragma once
// Bios : quels BIOS chaque console attend, lesquels sont présents dans le
// dossier bios/, et le rangement automatique d'un BIOS qu'on y dépose.
//
// Les BIOS sont reconnus à leur CONTENU, pas à leur nom : la base
// assets/database/System.dat (de libretro) donne la taille et l'empreinte
// CRC32 de chaque BIOS connu. Un fichier mal nommé est donc retrouvé quand
// même, et un fichier abîmé ou modifié est signalé.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class BiosNeed { Required, Recommended, Optional, None };

// Ce que l'empreinte dit d'un fichier présent.
enum class BiosCheck {
    Missing,  // pas de fichier
    Verified, // contenu identique au BIOS attendu
    Other,    // un BIOS connu, mais pas celui-là (mauvais nom)
    Unknown,  // inconnu de la base : abîmé, modifié, ou version rare
};

struct BiosFile {
    std::string label; // nom affiché
    std::string note;  // région, ou "Manquant"
    bool present = false;
    BiosCheck check = BiosCheck::Missing;
};

struct BiosSystem {
    const char* name;
    const char* systemIds; // consoles concernées, séparées par des espaces
    unsigned color;
    BiosNeed need;
    const char* needText;
    std::vector<BiosFile> files;
    bool ready = false;    // la console peut démarrer
};

class BiosManager {
public:
    BiosManager(std::filesystem::path biosDir, const std::filesystem::path& database);

    // Relit le dossier bios/ pour mettre les états à jour.
    void refresh();
    // Relit le dossier seulement s'il a changé depuis la dernière fois
    // (fichier ajouté, supprimé ou remplacé à la main dans l'Explorateur).
    // Renvoie true s'il a changé. Peu coûteux : à appeler régulièrement.
    bool refreshIfChanged();
    const std::vector<BiosSystem>& systems() const { return m_systems; }
    const BiosSystem* forSystem(const char* systemId) const;
    const std::filesystem::path& dir() const { return m_dir; }

    // Ce qu'est un fichier d'après la base : sa console et les noms (avec
    // leur sous-dossier) sous lesquels les cores le cherchent.
    struct Identity {
        std::string system;             // "Sony - PlayStation 2"
        std::vector<std::string> paths; // "pcsx2/bios/ps2-0230e-20080220.bin"...
    };
    std::optional<Identity> identify(const std::filesystem::path& file) const;

    // Copie le fichier au bon endroit s'il est reconnu. Renvoie un message
    // pour l'utilisateur.
    std::string import(const std::filesystem::path& file);
    // Cherche dans tout un dossier (et ses sous-dossiers) les fichiers
    // reconnus par leur empreinte, et les range.
    std::string importFolder(const std::filesystem::path& folder);

private:
    struct DbEntry {
        std::string system;
        std::string path; // relatif à bios/
        uint64_t size = 0;
        uint32_t crc = 0;
    };
    struct CachedCrc {
        uint64_t size = 0;
        long long time = 0;
        uint32_t crc = 0;
    };
    void loadDatabase(const std::filesystem::path& file);
    // Une empreinte de la liste des fichiers (noms, tailles, dates), pour
    // savoir si le dossier a changé sans relire leur contenu.
    uint64_t folderSignature() const;
    uint64_t m_signature = 0;
    std::optional<uint32_t> crcOf(const std::filesystem::path& file) const;
    BiosFile describe(const std::filesystem::path& rel, const std::string& label, const std::string& region) const;

    std::filesystem::path m_dir;
    std::vector<BiosSystem> m_systems;
    std::vector<DbEntry> m_db;
    std::map<uint64_t, std::vector<size_t>> m_bySize; // taille -> entrées de la base
    mutable std::map<std::filesystem::path, CachedCrc> m_crcCache;
};
