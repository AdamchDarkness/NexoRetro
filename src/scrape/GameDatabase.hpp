#pragma once
// GameDatabase : la base de jeux de libretro (un fichier .rdb par console).
//
// libretro-database (https://github.com/libretro/libretro-database) recense
// des dizaines de milliers de jeux : nom officiel, genre, développeur,
// éditeur, année, nombre de joueurs... Chaque jeu y est repéré par le CRC32
// de son fichier : si le CRC correspond, c'est forcément le bon jeu.
// Sinon, on se rabat sur le nom du fichier (NameIndex, plus bas).
//
// Format du .rdb : "RARCHDB\0", puis une suite d'objets MessagePack (un
// format binaire compact, comme du JSON en octets), un par jeu.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scrape {

// "Legend of Zelda, The - A Link to the Past (USA)" -> "legend of zelda a link to the past"
// Sert à comparer deux noms en ignorant la casse, la ponctuation, les
// informations entre parenthèses, "The" et la façon d'écrire les numéros
// (II = 2).
std::string matchKey(std::string_view name);

// Nom à afficher d'après le nom officiel : "Street Fighter II: The World
// Warrior (World 910522)" -> "Street Fighter II: The World Warrior". Sert
// pour l'arcade, où le fichier s'appelle seulement "sf2.zip".
std::string displayName(std::string_view officialName);

// Retrouver un nom dans une liste de noms "officiels" (No-Intro).
class NameIndex {
public:
    void add(const std::string& name);
    size_t size() const { return m_names.size(); }
    const std::string& name(size_t i) const { return m_names[i]; }

    // Le meilleur nom pour ce titre de jeu. region : celle du fichier
    // ("Europe"...), préférée si plusieurs versions existent.
    std::optional<size_t> find(std::string_view title, std::string_view region) const;
    // Recherche libre : les noms contenant tous les mots de la requête,
    // les plus pertinents d'abord.
    std::vector<size_t> search(std::string_view query, std::string_view region, size_t max) const;

private:
    std::optional<size_t> best(const std::vector<size_t>& candidates, std::string_view region) const;

    std::vector<std::string> m_names;
    std::unordered_multimap<std::string, size_t> m_byKey;  // nom complet
    std::unordered_multimap<std::string, size_t> m_byMain; // sans le sous-titre
    std::unordered_multimap<std::string, size_t> m_bySub;  // le sous-titre seul
};

// Préférence entre deux versions d'un même jeu : la région du fichier,
// puis Europe/France, et surtout pas une démo ou un prototype.
int versionScore(std::string_view name, std::string_view region);

struct DbGame {
    std::string name; // nom No-Intro complet : "Street Fighter II Turbo (USA)"
    std::string genre;
    std::string developer;
    std::string publisher;
    std::string franchise;
    int year = 0;
    int month = 0;
    int players = 0;
    uint32_t crc = 0;
    std::string fileName; // "kof98.zip"
};

class GameDatabase {
public:
    // bytes : le contenu d'un fichier .rdb.
    bool load(const std::string& bytes);
    size_t size() const { return m_games.size(); }

    const DbGame* byCrc(uint32_t crc) const;
    // Par le nom exact du fichier ("kof98.zip") : c'est ainsi qu'on
    // reconnaît un jeu d'arcade (le CRC d'un .zip dépend de sa compression).
    const DbGame* byFileName(std::string_view fileName) const;
    const DbGame* byTitle(std::string_view title, std::string_view region) const;

private:
    std::vector<DbGame> m_games;
    std::unordered_map<uint32_t, size_t> m_byCrc;
    std::unordered_map<std::string, size_t> m_byFile; // nom de fichier en minuscules
    NameIndex m_names; // même ordre que m_games
};

} // namespace scrape
