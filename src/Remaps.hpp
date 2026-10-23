#pragma once
// Remaps : "quel bouton de MA manette fait quoi" dans une console donnée.
//
// Les touches physiques donnent d'abord un RetroPad (Paramètres > Manettes).
// Ici, on choisit ensuite, pour chaque action de la console émulée (le bouton
// "1" de la Wiimote, "Z" sur N64...), quel bouton de ce RetroPad la déclenche.
//
// Mémorisé par console, et pour la Wii par type de manette (la Wiimote à
// l'horizontale ne se joue pas comme avec le Nunchuk). Fichier remaps.txt :
//   wii:WiiMote (sideways)<TAB>8<TAB>0
// = dans cette console, le bouton n°8 du core (A) vient du bouton n°0 (B).

#include <array>
#include <filesystem>
#include <map>
#include <string>

class Remaps {
public:
    static constexpr int kButtons = 16; // boutons du RetroPad (B, Y, Select, Start, croix, A, X, L, R, L2, R2, L3, R3)
    // Après les boutons, les deux sticks : [16] = stick gauche du core,
    // [17] = stick droit. Valeur : 0 = stick gauche de la manette, 1 = stick
    // droit, +2 = même stick avec l'axe vertical inversé, -1 = aucun.
    static constexpr int kLeftStick = 16;
    static constexpr int kSlots = 18;
    static constexpr int kNone = -1;    // action désactivée
    using Table = std::array<int, kSlots>; // [bouton ou stick du core] = bouton ou stick de la manette

    explicit Remaps(std::filesystem::path file);

    // La table d'une console : chaque bouton à lui-même par défaut.
    Table get(const std::string& key) const;
    void set(const std::string& key, int coreButton, int sourceButton);
    void reset(const std::string& key);
    static Table identity();
    static bool valid(int slot, int source);

private:
    void load();
    void save() const;

    std::filesystem::path m_file;
    std::map<std::string, Table> m_tables;
};
