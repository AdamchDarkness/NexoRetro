#pragma once
// Library : la liste des consoles gérées et des jeux trouvés sur le disque.
//
// Les jeux viennent de trois endroits :
//   roms/<console>/       un sous-dossier par console (roms/snes/...)
//   dossiers surveillés   ajoutés depuis l'écran "Ajouter des jeux"
//   fichiers ajoutés      un par un (glisser-déposer, "Choisir des fichiers")

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

class UserData;

struct System {
    const char* id;         // nom du sous-dossier dans roms/
    const char* name;       // nom affiché
    const char* shortName;  // sur les jaquettes : "PS2", "SNES"...
    unsigned color;         // couleur de la console, 0xRRGGBB
    const char* coreFile;   // DLL utilisée par défaut
    const char* extensions; // extensions reconnues, séparées par des espaces
    const char* hints;      // mots qui, dans un chemin, désignent cette console
    // Autres cores possibles, utilisés si le premier est absent.
    // [C++] Une valeur par défaut dans la struct : les consoles qui n'en
    // ont pas n'ont rien à écrire.
    const char* otherCores = nullptr;
    // Console sans core libretro (Switch, Xbox, PS3...) : le jeu est lancé
    // par un émulateur externe (un programme à part, que l'utilisateur
    // installe). Son nom de programme habituel, et ses arguments ({rom} =
    // le chemin du jeu). nullptr = console émulée par un core libretro.
    const char* externalName = nullptr; // "Ryujinx"
    const char* externalExe = nullptr;  // "Ryujinx.exe"
    const char* externalArgs = nullptr; // "{rom}"

    bool external() const { return externalExe != nullptr; }
};

struct Game {
    std::string title;           // nom nettoyé, en UTF-8
    std::string region;          // "Europe", "USA"... (d'après le nom du fichier)
    std::filesystem::path path;
    const System* system = nullptr;
    std::uintmax_t size = 0;     // en octets
    // Les seuls cores capables de lancer CE jeu (ex. un jeu d'arcade que
    // seul MAME connaît). nullptr = ceux de la console.
    const char* preferredCores = nullptr;
};

class Library {
public:
    Library(std::filesystem::path romsDir, std::filesystem::path coresDir);

    // [C++20] std::span : une "vue" sur un tableau (pointeur + taille),
    // l'équivalent sûr du couple (const System*, size_t) du C.
    static std::span<const System> systems();
    static const System* systemById(std::string_view id);
    // Les consoles qui acceptent cette extension (".iso" -> PS2, GameCube, PSP).
    static std::vector<const System*> candidatesFor(const std::filesystem::path& file);
    // Devine la console d'un fichier : extension, puis noms de dossiers.
    // nullptr si c'est impossible à dire.
    static const System* guessSystem(const std::filesystem::path& file);
    // Tous les fichiers d'un jeu : lui-même, plus les pistes d'un .cue ou
    // les disques d'un .m3u (pour le supprimer en entier).
    static std::vector<std::filesystem::path> filesOf(const std::filesystem::path& game);

    void scan(const UserData& userData);

    const std::vector<Game>& games() const { return m_games; }
    const Game* find(const std::filesystem::path& path) const;
    int countFor(const System& system) const;

    // Le core utilisé pour un jeu : le premier présent parmi ses cores
    // préférés, puis coreFile et otherCores de sa console. S'il n'y en a
    // aucun, le premier de la liste (pour dire lequel télécharger).
    std::string coreFileFor(const Game& game) const;
    std::filesystem::path corePath(const Game& game) const { return m_coresDir / coreFileFor(game); }
    bool hasCore(const Game& game) const;
    // Consoles à émulateur externe : où est son programme (vide = pas trouvé).
    std::filesystem::path externalPath(const System& system) const;
    void setExternalPath(const std::string& systemId, std::filesystem::path exe) { m_externalPaths[systemId] = std::move(exe); }

    // Les BIOS rangés parmi les jeux (neogeo.zip...) : pas affichés comme
    // des jeux, mais App peut les copier dans le dossier bios/.
    const std::vector<std::filesystem::path>& biosFound() const { return m_biosFound; }
    // Complète les jeux avec ce que d'autres modules savent d'eux (titre
    // trouvé en ligne, core conseillé...). change renvoie true s'il a
    // modifié le jeu ; la liste est alors retriée.
    void refine(const std::function<bool(Game&)>& change);

    // Dossier surveillé injoignable au dernier scan (NAS éteint, clé USB
    // débranchée...) : ses jeux sont absents, mais rien n'est oublié.
    bool isOffline(const std::filesystem::path& folder) const;
    // Un dossier sur le réseau : \\nas\jeux, ou un lecteur réseau (Z:).
    static bool isNetworkPath(const std::filesystem::path& folder);

    const std::filesystem::path& romsDir() const { return m_romsDir; }
    const std::filesystem::path& coresDir() const { return m_coresDir; }

private:
    void addGame(const std::filesystem::path& path, const System& system);

    std::filesystem::path m_romsDir;
    std::filesystem::path m_coresDir;
    std::vector<Game> m_games;
    std::vector<std::filesystem::path> m_biosFound;
    std::vector<std::string> m_offline; // clés (UserData::keyOf) des dossiers injoignables
    std::map<std::string, std::filesystem::path> m_externalPaths;
};

// "Super Mario World (USA) [!]" -> "Super Mario World"
std::string cleanTitle(const std::string& fileStem);
// "Final Fantasy X (France)" -> "France"
std::string regionOf(const std::string& fileStem);
