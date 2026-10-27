#pragma once
// TouchPad : la manette dessinée sur l'écran tactile (téléphones, tablettes).
//
// Croix directionnelle à gauche, boutons A B X Y à droite, gâchettes L / R
// (et L2 / R2) en haut, Select et Start en bas, un bouton de menu en haut au
// centre. Plusieurs doigts à la fois (croix + bouton), et on peut glisser
// d'un bouton à l'autre sans lever le doigt, comme sur une vraie manette.
//
// Les boutons appuyés vont au jeu par le même chemin qu'une vraie manette
// (Input::setTouchButtons). La manette tactile se cache toute seule dès
// qu'une vraie manette est branchée.

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>

namespace ui {

class TouchPad {
public:
    // Les doigts posés, levés ou déplacés.
    void handleEvent(const SDL_Event& event);
    // Recalcule les boutons appuyés (à chaque image), pour un écran de cette taille.
    void update(float width, float height);
    uint16_t buttons() const { return m_buttons; } // 1 bit par bouton du RetroPad
    // Le bouton de menu a été touché (une seule fois).
    bool takeMenu();
    // Dessine la manette (opacity : 0 à 1).
    void draw(float opacity) const;
    void clear(); // oublie les doigts (menu ouvert, fin de partie)

private:
    struct Layout {
        float unit = 0;             // la plus petite dimension de l'écran
        float dpadX = 0, dpadY = 0, dpadR = 0;
        float faceX = 0, faceY = 0, faceR = 0, faceGap = 0;
        float shoulderW = 0, shoulderH = 0, margin = 0;
        float width = 0, height = 0;
    };
    uint16_t buttonsAt(float x, float y) const;

    std::map<SDL_FingerID, SDL_FPoint> m_fingers; // position de chaque doigt (en points)
    Layout m_layout;
    uint16_t m_buttons = 0;
    uint16_t m_tapped = 0; // touchés depuis la dernière image (appuis très brefs)
    bool m_menuDown = false;
    bool m_menu = false;
};

} // namespace ui
