#pragma once
// Lang : l'interface en plusieurs langues.
//
// Le code est écrit en français ; chaque autre langue a un fichier
// assets/lang/<code>.txt qui traduit les textes, une ligne par texte :
//
//   # name: English
//   Jouer => Play
//   Il y a {} min => {} min ago
//
// "{}" remplace une partie variable (un nombre, un nom...). La traduction se
// fait au moment de DESSINER le texte (draw::text, draw::measure...) : le
// reste du code n'a pas à s'en soucier, et la mise en page s'adapte à la
// longueur des mots traduits. Pour ajouter une langue : copier en.txt,
// traduire la partie droite de chaque ligne.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lang {

struct Language {
    std::string code; // "en", "fr"...
    std::string name; // "English", "Français"...
};

// Les langues disponibles : le français (celle du code) + un fichier par langue.
std::vector<Language> available(const std::filesystem::path& dir);
// Charge une langue ("fr" = aucune traduction).
void setLanguage(const std::filesystem::path& dir, const std::string& code);
const std::string& current();
void newFrame(); // à chaque image (ménage du cache)

// Le texte dans la langue choisie (lui-même s'il n'est pas traduit). Le
// résultat reste valable jusqu'au prochain changement de langue.
// Langue écrite de droite à gauche (arabe) : le résultat est déjà prêt à
// dessiner (formes liées, ordre d'affichage : voir Arabic.hpp).
std::string_view tr(std::string_view text);

// La langue choisie s'écrit-elle de droite à gauche ?
bool rtl();
// La traduction dans l'ordre de lecture, AVANT la mise en forme de l'arabe :
// pour couper un paragraphe en lignes (chaque ligne est mise en forme à part).
std::string trLogical(std::string_view text);
// Tous les caractères des traductions chargées (pour préparer les polices).
std::vector<unsigned int> characters();

} // namespace lang
