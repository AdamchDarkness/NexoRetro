#pragma once
// Draw : les primitives de dessin "bas niveau" de l'interface.
//
// ImGui fournit une "draw list" : une liste de formes (rectangles arrondis,
// cercles, lignes, texte) à dessiner dans la fenêtre. Ce fichier ajoute
// par-dessus ce qui manque pour reproduire les maquettes : ombres douces,
// texte dans une graisse et une taille précises, et icônes vectorielles.
//
// Rien ici n'est cliquable : c'est le rôle de Widgets.hpp.

#include "Theme.hpp"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace draw {

// Ombre douce sous une carte : plusieurs rectangles arrondis de plus en
// plus grands et de plus en plus transparents (ImGui n'a pas de flou).
void shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, float offsetY, int alpha);

// Rectangle arrondi plein, avec bordure fine optionnelle.
void rect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 fill, float rounding, ImU32 border = 0);

// Bordure en pointillés (zones de dépôt).
void dashedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding, float thickness);

// --- Texte -------------------------------------------------------------
// size est en pixels de design (mis à l'échelle automatiquement).
ImVec2 measure(theme::Weight weight, float size, std::string_view text);
void text(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view text);
// Coupe le texte avec "…" s'il dépasse maxWidth (en pixels réels).
void textEllipsis(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view text,
                  float maxWidth);
// Texte sur plusieurs lignes ; renvoie la hauteur occupée.
float textWrapped(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view text,
                  float wrapWidth);
float wrappedHeight(theme::Weight weight, float size, std::string_view text, float wrapWidth);

// --- Icônes ------------------------------------------------------------
// Dessinées en traits, sur une grille de 24 x 24 (comme les SVG des maquettes).
enum class Icon {
    None, Play, Plus, Search, Close, Check, ChevronLeft, ChevronRight, Folder, Upload,
    Chip, Image, Refresh, Users, Heart, HeartOutline, Gamepad, Copy, Pause, Pencil, Trash,
    Mic, MicOff, Speaker, SpeakerOff, Grid, List, Carousel, Filter, Tv,
    ChevronDown, Box, Cartridge, Disc, Palette
};
void icon(ImDrawList* dl, Icon icon, ImVec2 topLeft, float size, ImU32 color);

// Langue de droite à gauche (arabe) : un paragraphe coupé en lignes de
// wrapWidth au plus, chacune prête à dessiner.
std::vector<std::string> rtlLines(theme::Weight weight, float size, std::string_view value, float wrapWidth);

} // namespace draw
