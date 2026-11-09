#pragma once
// AchievementPopup : le bandeau "Succès débloqué" qui apparaît en haut de
// l'écran pendant le jeu, avec l'image du succès et ses points.
//
// Les messages arrivent d'Achievements (voir Achievements.hpp) et s'affichent
// l'un après l'autre, quelques secondes chacun.

#include "../Achievements.hpp"

#include <SDL3/SDL.h>

#include <deque>

class Covers;

namespace ui {

class AchievementPopups {
public:
    void push(Achievements::Notice notice);
    // Au premier plan, par-dessus le jeu et les menus.
    void draw(const Covers& covers);

private:
    std::deque<Achievements::Notice> m_queue;
    Uint64 m_shownAt = 0; // quand le premier de la file est apparu (0 = pas encore)
};

// La liste des succès du jeu en cours (menu pause > Succès), par-dessus le
// jeu. Renvoie false quand on la referme.
bool drawAchievementList(const Achievements& achievements, const Covers& covers, const std::string& gameTitle);

} // namespace ui
