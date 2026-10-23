#pragma once
// Arabic : afficher de l'arabe avec une bibliothèque de dessin qui ne sait
// écrire que de gauche à droite, une lettre après l'autre (ImGui).
//
// Deux transformations, faites une fois par texte :
//
// 1. Les formes liées. En arabe, une lettre change de dessin selon qu'elle
//    est seule, au début, au milieu ou à la fin d'un mot (ب : ﺏ ﺑ ﺒ ﺐ).
//    Unicode a un caractère pour chaque forme ("formes de présentation",
//    U+FE70 à U+FEFF) : on remplace chaque lettre par la bonne forme. Lam +
//    alif devient une seule ligature (ﻻ).
//
// 2. L'ordre d'affichage. L'arabe se lit de droite à gauche, mais les
//    nombres et les mots latins ("PlayStation 2", "64 Mo") de gauche à
//    droite. On découpe le texte en morceaux de même sens, on inverse
//    l'ordre des morceaux, et les lettres des morceaux arabes. C'est une
//    version simplifiée de l'algorithme bidirectionnel d'Unicode.
//
// Un texte sans lettre arabe ressort tel quel : la transformation peut donc
// être appliquée deux fois sans dégât (le résultat ne contient plus que des
// formes de présentation).

#include <string>
#include <string_view>

namespace arabic {

// Contient au moins une lettre arabe "logique" (U+0621 à U+064A) ?
bool hasArabic(std::string_view utf8);
// Formes liées + ordre d'affichage, en UTF-8.
std::string visual(std::string_view utf8);

} // namespace arabic
