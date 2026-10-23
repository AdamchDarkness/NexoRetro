#pragma once
// Theme : les "jetons de design" de l'interface, c'est-à-dire toutes les
// valeurs visuelles de base (couleurs, polices, échelle), au même endroit.
//
// Ce sont exactement les valeurs des maquettes. Pour changer l'apparence de
// toute l'application (par exemple la couleur d'accent), c'est ICI qu'il
// faut modifier, pas dans chaque écran.

#include <imgui.h>

#include <filesystem>

namespace theme {

// --- Couleurs -----------------------------------------------------------
// IM_COL32(r, g, b, a) fabrique une couleur 32 bits pour ImGui.
constexpr ImU32 Ground = IM_COL32(0xF4, 0xF4, 0xF1, 255);      // fond de l'application
constexpr ImU32 Surface = IM_COL32(0xFF, 0xFF, 0xFF, 255);     // cartes blanches
constexpr ImU32 SurfaceAlt = IM_COL32(0xFA, 0xFA, 0xF8, 255);  // zones de dépôt, lignes
constexpr ImU32 Ink = IM_COL32(0x11, 0x11, 0x14, 255);         // texte principal, boutons noirs
constexpr ImU32 InkHover = IM_COL32(0x2A, 0x2A, 0x30, 255);
constexpr ImU32 Body = IM_COL32(0x3A, 0x3A, 0x42, 255);        // paragraphes
constexpr ImU32 Muted = IM_COL32(0x5E, 0x5E, 0x66, 255);       // texte secondaire
constexpr ImU32 Hairline = IM_COL32(0xE4, 0xE4, 0xDF, 255);    // bordures fines
constexpr ImU32 Divider = IM_COL32(0xEC, 0xEC, 0xE8, 255);     // séparateurs
constexpr ImU32 Dashed = IM_COL32(0xC9, 0xC9, 0xC2, 255);      // bordure pointillée
constexpr ImU32 Track = IM_COL32(0xD6, 0xD6, 0xD0, 255);       // interrupteur éteint
constexpr ImU32 Placeholder = IM_COL32(0xDC, 0xDD, 0xE6, 255); // vignettes vides

constexpr ImU32 Accent = IM_COL32(0x1F, 0x4F, 0xFF, 255);      // bleu cobalt
constexpr ImU32 AccentHover = IM_COL32(0x12, 0x38, 0xC9, 255);
constexpr ImU32 AccentSoft = IM_COL32(0xEE, 0xF2, 0xFF, 255);

constexpr ImU32 Success = IM_COL32(0x15, 0x75, 0x3F, 255);
constexpr ImU32 SuccessSoft = IM_COL32(0xE3, 0xF4, 0xEA, 255);
constexpr ImU32 Warning = IM_COL32(0x8A, 0x3D, 0x06, 255);
constexpr ImU32 WarningSoft = IM_COL32(0xFF, 0xF1, 0xE0, 255);
constexpr ImU32 Danger = IM_COL32(0xA3, 0x21, 0x1B, 255);
constexpr ImU32 DangerSoft = IM_COL32(0xFD, 0xE8, 0xE7, 255);
constexpr ImU32 NeutralSoft = IM_COL32(0xF0, 0xF0, 0xEC, 255);
constexpr ImU32 Favorite = IM_COL32(0xE5, 0x48, 0x4D, 255);

// Couleur 0xRRGGBB (comme en CSS) -> couleur ImGui.
constexpr ImU32 rgb(unsigned hex, int alpha = 255)
{
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
}

// La même couleur avec une autre transparence (0 à 255).
ImU32 withAlpha(ImU32 color, int alpha);

// --- Polices ------------------------------------------------------------
// Plus Jakarta Sans en quatre graisses. Les tailles sont libres : ImGui 1.92
// génère chaque taille à la demande.
enum class Weight { Medium, SemiBold, Bold, ExtraBold };

bool loadFonts(const std::filesystem::path& fontDir);
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
