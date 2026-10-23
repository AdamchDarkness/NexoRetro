#pragma once
// Widgets : les composants interactifs de l'interface (boutons en pilule,
// puces de filtre, interrupteurs...).
//
// Ils s'utilisent comme les widgets d'ImGui : on les appelle à chaque image,
// ils se placent à la position courante du curseur ImGui, et renvoient true
// l'image où l'on clique dessus. La différence : ils se dessinent eux-mêmes
// (avec Draw.hpp) pour ressembler exactement aux maquettes.

#include "Draw.hpp"
#include "Theme.hpp"

#include <imgui.h>

#include <span>
#include <string_view>

namespace ui {

// --- Boutons -----------------------------------------------------------
enum class ButtonStyle {
    Primary, // fond noir, texte blanc : l'action principale
    Accent,  // fond bleu : "Ajouter"
    Outline, // fond blanc, bordure fine
    Soft,    // fond gris très clair
    Ghost,   // sans fond (liens, retours)
    Danger   // fond rouge : action destructrice (supprimer)
};

struct ButtonOptions {
    ButtonStyle style = ButtonStyle::Outline;
    draw::Icon icon = draw::Icon::None;
    float height = 44.0f;   // pixels de design
    float width = 0.0f;     // 0 = selon le texte ; < 0 = toute la largeur disponible
    float fontSize = 14.0f;
    bool disabled = false;
    const char* tooltip = nullptr;
};

// label peut contenir "##id" pour distinguer deux boutons au même texte.
bool button(const char* label, const ButtonOptions& options = {});
bool iconButton(const char* id, draw::Icon icon, ButtonStyle style, float size, const char* tooltip = nullptr,
                bool disabled = false);
// Texte bleu cliquable ("Tout voir").
bool link(const char* label, float size = 14.0f);

// --- Sélection ---------------------------------------------------------
// Puce de filtre avec pastille de couleur et compteur.
bool chip(const char* id, const char* label, const char* count, ImU32 dot, bool selected);
float chipWidth(const char* label, const char* count); // pour savoir s'il faut passer à la ligne
// Barre à faire glisser (volume...). suffix : affiché après la valeur ("%").
bool slider(const char* id, int* value, int minValue, int maxValue, float width, const char* suffix = nullptr);
// Interrupteur marche/arrêt.
bool toggle(const char* id, bool* value, bool disabled = false);
// Choix exclusif entre quelques options côte à côte.
bool segmented(const char* id, std::span<const char* const> labels, int* current, bool disabled = false);
// Liste déroulante en forme de pilule.
bool combo(const char* id, std::span<const char* const> items, int* current, float width);

// --- Saisie ------------------------------------------------------------
bool searchField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool focus = false);
bool textField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool disabled = false);

// --- Affichage ---------------------------------------------------------
// Info : gris sur fond gris clair (dans une carte blanche).
// Light : gris sur fond blanc (directement sur le fond de la page).
enum class Tone { Success, Warning, Danger, Neutral, Accent, Info, Light };
ImVec2 pillSize(const char* text, float fontSize = 12.0f);
void pill(const char* text, Tone tone, float fontSize = 12.0f); // pastille de statut, au curseur
void pillAt(ImDrawList* dl, ImVec2 pos, const char* text, Tone tone, float fontSize = 12.0f);

void avatar(ImDrawList* dl, ImVec2 center, float radius, const char* initial, ImU32 bg, ImU32 fg, ImU32 dot = 0,
            ImU32 ring = theme::Surface);

// Texte au curseur, dans une graisse et une taille précises.
void text(std::string_view value, float size = 14.0f, theme::Weight weight = theme::Weight::Medium,
          ImU32 color = theme::Ink);
void textWrapped(std::string_view value, float size, theme::Weight weight, ImU32 color, float wrapWidth);
// Petit titre en capitales espacées ("SAUVEGARDES D'ÉTAT").
void overline(std::string_view value);

// --- Cartes ------------------------------------------------------------
// Une carte blanche arrondie qui contient d'autres widgets. Entre
// beginCard() et endCard(), le curseur est à l'intérieur, marges comprises.
void beginCard(const char* id, ImVec2 size, float padding = 22.0f, ImU32 fill = theme::Surface, float rounding = 24.0f,
               bool scroll = false);
void endCard();

// Espace vertical ou horizontal, en pixels de design.
void space(float designPixels);
void sameLine(float spacing = 10.0f);

} // namespace ui
