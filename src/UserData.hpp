#pragma once
// UserData : tout ce que l'émulateur retient sur vos jeux entre deux
// lancements (dernière partie, temps de jeu, favoris, jaquette choisie),
// ainsi que les dossiers surveillés et les jeux ajoutés à la main.
//
// C'est enregistré dans un simple fichier texte (userdata.txt), une ligne
// par information, lisible avec le Bloc-notes.

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

struct GameRecord {
    long long lastPlayed = 0;  // date (secondes depuis 1970), 0 = jamais
    long long playSeconds = 0; // temps de jeu total
    bool favorite = false;
    std::string cover;         // image choisie à la main (UTF-8), vide = générée
    std::string title;         // nom choisi à la main, vide = nom tiré du fichier
    std::string controller;    // manette émulée choisie (Wii : "WiiMote + Nunchuk"...), vide = celle par défaut
};

class UserData {
public:
    explicit UserData(std::filesystem::path file);

    void load();
    void save() const;

    // Une clé unique par fichier : son chemin complet, normalisé, en UTF-8.
    static std::string keyOf(const std::filesystem::path& path);

    GameRecord& record(const std::filesystem::path& game);
    const GameRecord* find(const std::filesystem::path& game) const;

    const std::vector<std::filesystem::path>& watchedFolders() const { return m_folders; }
    void addWatchedFolder(const std::filesystem::path& folder);
    void removeWatchedFolder(const std::filesystem::path& folder);

    // Fichier ajouté à la main -> identifiant de sa console.
    const std::map<std::string, std::string>& manualGames() const { return m_manual; }
    void addManualGame(const std::filesystem::path& file, const std::string& systemId);

    // Jeux retirés de la bibliothèque (le fichier reste sur le disque).
    void hide(const std::filesystem::path& game);
    bool isHidden(const std::filesystem::path& game) const;
    size_t hiddenCount() const { return m_hidden.size(); }
    void unhideAll() { m_hidden.clear(); }
    // Oublie tout d'un jeu (son fichier a été supprimé).
    void forget(const std::filesystem::path& game);

    // Copie portable (clé USB) : écrit ces données dans un autre fichier,
    // pour un dossier de données ailleurs. "moved" : jeux recopiés dans le
    // nouveau dossier (ancienne clé -> nouveau chemin complet). Les dossiers
    // surveillés locaux et les jeux ajoutés à la main sont alors inutiles
    // (leurs jeux sont dans roms/) ; les dossiers réseau restent.
    void exportTo(const std::filesystem::path& file, const std::map<std::string, std::string>& moved) const;

private:
    std::filesystem::path m_file;
    std::filesystem::path m_base; // le dossier des données (chemins relatifs)
    std::map<std::string, GameRecord> m_games;
    std::vector<std::filesystem::path> m_folders;
    std::map<std::string, std::string> m_manual;
    std::set<std::string> m_hidden;
};
