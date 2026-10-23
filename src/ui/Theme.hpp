#pragma once
// Theme : les "jetons de design" de l'interface, c'est-à-dire toutes les
// valeurs visuelles de base (couleurs, polices, échelle), au même endroit.
//
// Les valeurs écrites ici sont celles des maquettes (le thème « Clair »).
// Les écrans n'écrivent jamais une couleur en dur : ils utilisent ces noms
// (theme::Ink, theme::Accent...). Changer de thème (Paramètres > Apparence,
// voir Themes.hpp) remplace toutes ces couleurs d'un coup, et toute
// l'interface suit.
// (Les couleurs vives des consoles, des boutons de manette ou du mode Big
// Picture, toujours sombre, ne dépendent pas du thème.)

#include <imgui.h>

#include <filesystem>
#include <string>

namespace theme {

// Couleur 0xRRGGBB (comme en CSS) -> couleur ImGui.
// IM_COL32(r, g, b, a) fabrique une couleur 32 bits pour ImGui.
constexpr ImU32 rgb(unsigned hex, int alpha = 255)
{
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
}

// --- Couleurs -----------------------------------------------------------
// Toutes les couleurs d'un thème. Un thème (Themes.hpp) en fournit une
// complète ; setPalette() l'applique.
struct Palette {
    ImU32 ground;      // fond de l'application
    ImU32 surface;     // cartes (blanches dans le thème clair)
    ImU32 surfaceAlt;  // zones de dépôt, lignes survolées
    ImU32 ink;         // texte principal, boutons "noirs"
    ImU32 inkHover;
    ImU32 body;        // paragraphes
    ImU32 muted;       // texte secondaire
    ImU32 hairline;    // bordures fines
    ImU32 divider;     // séparateurs
    ImU32 dashed;      // bordure pointillée
    ImU32 track;       // interrupteur éteint
    ImU32 placeholder; // vignettes vides
    ImU32 accent;      // la couleur de la marque (bleu cobalt)
    ImU32 accentHover;
    ImU32 accentSoft;  // fond teinté d'accent (pastilles, sélection)
    ImU32 accentInk;   // texte posé sur accentSoft
    ImU32 onAccent;    // texte posé sur accent (ou sur danger)
    ImU32 success, successSoft;
    ImU32 warning, warningSoft;
    ImU32 danger, dangerSoft;
    ImU32 neutralSoft;
    ImU32 favorite;    // le cœur des favoris
    ImU32 coverInk;    // texte des jaquettes générées
};

// Les couleurs du thème en cours. Ce ne sont plus des constantes : elles
// changent quand on change de thème.
// [C++] "inline" sur une variable (C++17) : elle est définie dans l'en-tête,
// mais n'existe qu'UNE fois dans tout le programme (en C, il faudrait écrire
// "extern" ici et la définition dans un seul .cpp).
inline ImU32 Ground = IM_COL32(0xF4, 0xF4, 0xF1, 255);
inline ImU32 Surface = IM_COL32(0xFF, 0xFF, 0xFF, 255);
inline ImU32 SurfaceAlt = IM_COL32(0xFA, 0xFA, 0xF8, 255);
inline ImU32 Ink = IM_COL32(0x11, 0x11, 0x14, 255);
inline ImU32 InkHover = IM_COL32(0x2A, 0x2A, 0x30, 255);
inline ImU32 Body = IM_COL32(0x3A, 0x3A, 0x42, 255);
inline ImU32 Muted = IM_COL32(0x5E, 0x5E, 0x66, 255);
inline ImU32 Hairline = IM_COL32(0xE4, 0xE4, 0xDF, 255);
inline ImU32 Divider = IM_COL32(0xEC, 0xEC, 0xE8, 255);
inline ImU32 Dashed = IM_COL32(0xC9, 0xC9, 0xC2, 255);
inline ImU32 Track = IM_COL32(0xD6, 0xD6, 0xD0, 255);
inline ImU32 Placeholder = IM_COL32(0xDC, 0xDD, 0xE6, 255);

inline ImU32 Accent = IM_COL32(0x1F, 0x4F, 0xFF, 255); // bleu cobalt
inline ImU32 AccentHover = IM_COL32(0x12, 0x38, 0xC9, 255);
inline ImU32 AccentSoft = IM_COL32(0xEE, 0xF2, 0xFF, 255);
inline ImU32 AccentInk = IM_COL32(0x1E, 0x2A, 0x78, 255);
inline ImU32 OnAccent = IM_COL32(0xFF, 0xFF, 0xFF, 255);

inline ImU32 Success = IM_COL32(0x15, 0x75, 0x3F, 255);
inline ImU32 SuccessSoft = IM_COL32(0xE3, 0xF4, 0xEA, 255);
inline ImU32 Warning = IM_COL32(0x8A, 0x3D, 0x06, 255);
inline ImU32 WarningSoft = IM_COL32(0xFF, 0xF1, 0xE0, 255);
inline ImU32 Danger = IM_COL32(0xA3, 0x21, 0x1B, 255);
inline ImU32 DangerSoft = IM_COL32(0xFD, 0xE8, 0xE7, 255);
inline ImU32 NeutralSoft = IM_COL32(0xF0, 0xF0, 0xEC, 255);
inline ImU32 Favorite = IM_COL32(0xE5, 0x48, 0x4D, 255);
inline ImU32 CoverInk = IM_COL32(0xFF, 0xFF, 0xFF, 255);

// Du blanc qui reste blanc, quel que soit le thème (texte sur une photo...).
constexpr ImU32 White = IM_COL32(0xFF, 0xFF, 0xFF, 255);

// Remplace toutes les couleurs ci-dessus, et celles des widgets d'ImGui.
void setPalette(const Palette& palette);
// Les couleurs en cours, rangées dans une Palette.
Palette palette();
// Le thème en cours est-il sombre (texte clair sur fond foncé) ?
bool isDark();

// La même couleur avec une autre transparence (0 à 255).
ImU32 withAlpha(ImU32 color, int alpha);
// Mélange de deux couleurs : t = 0 donne a, t = 1 donne b.
ImU32 mix(ImU32 a, ImU32 b, float t);
// Luminosité perçue d'une couleur, de 0 (noir) à 1 (blanc).
float luminance(ImU32 color);

// --- Polices ------------------------------------------------------------
// Plus Jakarta Sans en quatre graisses. Les tailles sont libres : ImGui 1.92
// génère chaque taille à la demande.
enum class Weight { Medium, SemiBold, Bold, ExtraBold };

// language : les polices de secours (chinois, japonais, coréen, arabe) sont
// rangées dans l'ordre qui convient à cette langue.
bool loadFonts(const std::filesystem::path& fontDir, const std::string& language);
// Les polices chargées conviennent-elles à cette langue et à la police
// choisie ? (Sinon : les recharger.)
bool fontsMatch(const std::string& language);
// Police personnalisée (.ttf / .otf) pour toute l'interface ; chemin vide =
// Plus Jakarta Sans. Prise en compte au prochain rechargement (fontsMatch).
void setCustomFont(const std::filesystem::path& file);
// Le fichier est-il une police que l'on sait lire ? (TrueType ou OpenType)
bool isFontFile(const std::filesystem::path& file);
ImFont* font(Weight weight);
// Prépare d'avance toutes les lettres, dans toutes les tailles utilisées.
// À appeler pendant la première image (après ImGui::NewFrame).
void prewarmFonts();

// --- Échelle ------------------------------------------------------------
// Les maquettes sont dessinées en "pixels de design". Sur un écran réglé à
// 150 % dans Windows, tout doit être 1,5 fois plus grand : px() s'en charge.
void setScale(float scale);
float scale();
inline float px(float designPixels) { return designPixels * scale(); }
inline ImVec2 px(float x, float y) { return ImVec2(px(x), px(y)); }

// Réglages de base d'ImGui (couleurs des widgets standards, arrondis...).
void applyImGuiStyle();

} // namespace theme
