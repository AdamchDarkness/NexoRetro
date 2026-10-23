#pragma once
// Les manettes de la Wii, telles que Dolphin sait les émuler.
//
// Sur Wii, la "manette" dépend du jeu : une Wiimote seule tenue à
// l'horizontale (Mario Kart, New Super Mario Bros.), une Wiimote avec son
// Nunchuk (Mario Galaxy, Zelda), une manette classique (jeux de combat), ou
// même une manette GameCube. Dolphin les annonce par leur nom
// (SET_CONTROLLER_INFO) ; on choisit, jeu par jeu, laquelle brancher.

#include <iterator>
#include <string_view>

struct WiiController {
    const char* label;   // ce qu'on affiche
    const char* dolphin; // le nom donné par Dolphin
    const char* help;    // comment elle se joue avec une manette de PC
};

inline constexpr WiiController kWiiControllers[] = {
    {"Wiimote + Nunchuk", "WiiMote + Nunchuk",
     "Stick gauche : stick du Nunchuk · stick droit : pointeur · la plupart des jeux Wii"},
    {"Manette classique", "WiiMote + Classic Controller",
     "Comme une manette de PC : pour les jeux qui l'acceptent (combat, rétro)"},
    {"Wiimote à l'horizontale", "WiiMote (sideways)",
     "Tenue comme une manette NES : Mario Kart, New Super Mario Bros…"},
    {"Wiimote seule", "WiiMote (upright)", "Tenue droite, pour viser : stick droit = pointeur"},
    {"Wiimote + MotionPlus + Nunchuk", "WiiMote + MotionPlus + Nunchuk", "Pour les jeux MotionPlus (Zelda Skyward Sword…)"},
    {"Manette GameCube", "GameCube Controller", "Pour les jeux Wii compatibles GameCube (Smash Bros. Brawl…)"},
};

// La manette par défaut : celle qui marche avec le plus de jeux.
inline constexpr std::string_view kDefaultWiiController = "WiiMote + Nunchuk";

inline const WiiController* wiiControllerByDolphinName(std::string_view name)
{
    for (const WiiController& c : kWiiControllers)
        if (name == c.dolphin)
            return &c;
    return nullptr;
}
