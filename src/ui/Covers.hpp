#pragma once
// Covers : le dessin des jaquettes.
//
// - Jaquette générée : un aplat de couleur, le nom court de la console en
//   haut et le titre en gros en bas (comme dans les maquettes).
// - Jaquette personnalisée : une image choisie par l'utilisateur, chargée
//   avec stb_image et envoyée à la carte graphique (texture OpenGL).
// - Jaquette trouvée en ligne par le scraper.
// Ordre de priorité : personnalisée, puis trouvée en ligne, puis générée.

#include "../Library.hpp"
#include "../UserData.hpp"

#include <imgui.h>

#include <map>
#include <string>

namespace scrape {
class Scraper;
}

class Covers {
public:
    Covers() = default;
    ~Covers();
    Covers(const Covers&) = delete;
    Covers& operator=(const Covers&) = delete;

    void setScraper(const scrape::Scraper* scraper) { m_scraper = scraper; }
    // À appeler une fois par image (limite le nombre d'images chargées par image).
    void newFrame() { m_loadsThisFrame = 0; }

    // Dessine la jaquette du jeu dans le rectangle [min, max].
    // titleSize : taille du titre en pixels de design (0 = pas de titre).
    void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const Game& game, const UserData& userData, float rounding,
              float titleSize) const;
    // Seulement une VRAIE image (choisie à la main ou trouvée en ligne) :
    // renvoie false s'il n'y en a pas (rien n'est dessiné). Pour les vues qui
    // dessinent elles-mêmes la jaquette générée (CD...).
    bool drawArt(ImDrawList* dl, ImVec2 min, ImVec2 max, const Game& game, const UserData& userData,
                 float rounding) const;
    // Dessine une image quelconque (jaquette proposée, capture d'écran...)
    // en remplissant le rectangle. Renvoie false si elle n'est pas (encore) chargée.
    // maxSize : taille maximale gardée en mémoire (plus pour un fond d'écran).
    bool drawImage(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::string& file, float rounding,
                   int maxSize = 720) const;

    // Couleur de la jaquette générée (différente pour chaque titre, prise
    // parmi celles du thème).
    static ImU32 colorFor(const Game& game);

    // Les textures appartiennent au contexte OpenGL : il faut les libérer
    // avant que l'émulateur ne le détruise (au lancement d'un jeu).
    void releaseTextures();

private:
    struct Texture {
        unsigned id = 0; // texture OpenGL (0 = pas encore chargée ou échec)
        int width = 0;
        int height = 0;
        bool failed = false;
    };
    const Texture* texture(const std::string& file, int maxSize) const;

    // "mutable" : le cache peut se remplir pendant draw(), qui est const
    // (dessiner ne change pas la liste des jaquettes, seulement le cache).
    mutable std::map<std::string, Texture> m_textures;
    mutable int m_loadsThisFrame = 0;
    const scrape::Scraper* m_scraper = nullptr;
};
