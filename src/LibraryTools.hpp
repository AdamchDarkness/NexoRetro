#pragma once
// LibraryTools : ce qu'on peut faire de la bibliothèque tout entière.
//
//  - l'exporter en tableau (CSV, pour un tableur) ou en page web (HTML) :
//    une liste de sa collection à garder, imprimer ou partager ;
//  - y trouver les doublons (le même fichier en double) et les versions
//    multiples d'un même jeu (Europe, USA, Japon, révisions...).

#include "Library.hpp"
#include "UserData.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace scrape {
class Scraper;
}

namespace library_tools {

// Écrit le fichier, renvoie un message pour l'utilisateur.
std::string exportCsv(const std::vector<Game>& games, const UserData& userData, const scrape::Scraper& scraper,
                      const std::filesystem::path& file);
std::string exportHtml(const std::vector<Game>& games, const UserData& userData, const scrape::Scraper& scraper,
                       const std::filesystem::path& file);

// Un même jeu (même console, même nom) présent plusieurs fois.
struct Duplicates {
    std::vector<const Game*> games;
    bool identical = false; // tous le même fichier (sinon : régions ou versions différentes)
};
// [C++] Les pointeurs visent les jeux de "games" : à recalculer après un
// nouveau scan de la bibliothèque.
std::vector<Duplicates> findDuplicates(const std::vector<Game>& games);

} // namespace library_tools
