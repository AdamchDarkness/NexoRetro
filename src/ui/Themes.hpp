#pragma once
// Themes : les thèmes de l'interface (Paramètres > Apparence).
//
// Un thème, c'est :
//   - une palette complète (toutes les couleurs de Theme.hpp) ;
//   - un fond : uni, dégradé de deux couleurs (avec un angle, comme sur
//     Discord) ou image ;
//   - une police (vide = Plus Jakarta Sans) ;
//   - un pack de sons pour l'interface (UiSounds.hpp) ;
//   - les couleurs des jaquettes générées.
//
// Cinq thèmes sont intégrés (Clair, Sombre, Nuit OLED, Synthwave, Game Boy).
// D'autres peuvent être ajoutés SANS recompiler : un fichier texte par
// thème dans <données>/themes/, lu au démarrage. Exemple, océan.ini :
//
//   # Mon thème
//   name=Océan
//   base=sombre                  (le thème de départ : les couleurs non
//                                 précisées viennent de lui)
//   accent=#22D3EE
//   ground=#0B1622               (et surface, ink, muted... : voir kKeys
//                                 dans Themes.cpp pour la liste complète)
//   background=gradient          (solid, gradient ou image)
//   gradientFrom=#0B1622
//   gradientTo=#1E3A5F
//   gradientAngle=135            (en degrés : 0 = de gauche à droite,
//                                 90 = de haut en bas)
//   image=fond.png               (relatif au dossier themes/)
//   imageDim=50                  (voile sur l'image, en %)
//   font=fonts/MaPolice.ttf      (relatif au dossier themes/)
//   sounds=8bits                 (doux, 8bits ou arcade)
//   covers=#123456,#654321       (couleurs des jaquettes générées)
//
// Par-dessus le thème choisi, les réglages de l'utilisateur (couleur
// d'accent, fond, image, police...) forment le thème « actif ».

#include "Theme.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct Settings;
class Covers;

namespace theme {

enum class Background { Solid, Gradient, Image };

struct ThemeInfo {
    std::string id;   // "clair", "sombre"... ou le nom du fichier .ini
    std::string name; // affiché (en français : traduit au dessin)
    Palette palette{};
    Background background = Background::Solid;
    ImU32 gradientFrom = 0;
    ImU32 gradientTo = 0;
    int gradientAngle = 90;
    std::filesystem::path image; // chemin complet
    int imageDim = 50;           // voile sur l'image, 0 à 90 %
    std::filesystem::path font;  // chemin complet ; vide = police d'origine
    int soundPack = 0;           // 0 = doux, 1 = 8 bits, 2 = arcade
    std::vector<ImU32> covers;   // couleurs des jaquettes générées
};

// Lit les thèmes : les intégrés, puis les fichiers <dossier>/*.ini.
void loadThemes(const std::filesystem::path& dir);
const std::filesystem::path& themesDir();
const std::vector<ThemeInfo>& themes();
// Le thème de cet identifiant (le Clair s'il n'existe pas).
const ThemeInfo& findTheme(const std::string& id);

// Applique le thème choisi dans les réglages, avec la personnalisation de
// l'utilisateur (accent, fond, image, police) : couleurs, fond et police.
void applySettings(const Settings& settings);
// Le thème actif (après personnalisation).
const ThemeInfo& active();

// Couleur de jaquette générée n° hash (dans les couleurs du thème actif).
ImU32 coverColor(size_t hash);

// Dessine le fond de l'interface (uni, dégradé ou image voilée) dans le
// rectangle [min, max].
void drawBackground(ImDrawList* dl, ImVec2 min, ImVec2 max, const Covers& covers);
// Un dégradé linéaire de deux couleurs, dans la direction angleDeg.
void gradientRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 from, ImU32 to, int angleDeg, float rounding = 0.0f);

// "#RRGGBB" <-> couleur. parseColor renvoie false si le texte n'en est pas une.
bool parseColor(const std::string& text, ImU32& out);
std::string formatColor(ImU32 color);

// Une palette recalculée autour d'une autre couleur d'accent, ou d'une
// autre couleur de fond (les cartes, bordures et textes suivent, pour
// rester lisibles).
Palette withAccent(Palette palette, ImU32 accent);
Palette withGround(Palette palette, ImU32 ground);

} // namespace theme
