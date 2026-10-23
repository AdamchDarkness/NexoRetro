#pragma once
// PauseMenu : le menu affiché par-dessus le jeu (Échap, ou raccourci manette).

#include <array>
#include <string>
#include <vector>

namespace ui {

enum class PauseAction { None, Resume, SaveState, LoadState, Reset, Screenshot, Fullscreen, Controls, Library, Quit };

// Une ligne de l'écran "Commandes" : une action de la console émulée.
struct ControlRow {
    int coreButton = -1;     // n° du bouton RetroPad que le core lit ; 16/17 = son stick gauche/droit
    std::string description; // ce que le core en fait ("Wiimote 1", "Shake"...)
};

// Ce que l'utilisateur a fait dans l'écran "Commandes".
struct ControlsResult {
    int coreButton = -1; // >= 0 : ce bouton du core vient maintenant de "source"
    int source = 0;      // bouton de la manette, -1 = aucun
    bool reset = false;  // tout remettre comme à l'origine
    bool back = false;   // retour au menu pause
};

class PauseMenu {
public:
    // À appeler quand le menu s'ouvre, pour placer le curseur sur "Reprendre".
    void open() { m_justOpened = true; }
    // volume : modifiable par la barre du menu ; renvoyé dans *volumeChanged.
    PauseAction draw(const std::string& gameTitle, const std::string& systemName, int& volume, bool& volumeChanged);
    // L'écran "Commandes" : pour chaque action de la console, le bouton de
    // la manette qui la déclenche (table : [bouton du core] = bouton de la manette).
    ControlsResult drawControls(const std::string& subtitle, const std::vector<ControlRow>& rows,
                                const std::array<int, 18>& table);

private:
    bool m_justOpened = false;
};

} // namespace ui
