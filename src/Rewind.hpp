#pragma once
// Rewind : le retour en arrière. On garde une "photo" de l'état du jeu à
// chaque image, pour pouvoir remonter le temps image par image.
//
// Une photo fait de quelques Ko (NES) à plusieurs centaines de Ko (SNES) :
// 60 par seconde rempliraient vite la mémoire. Mais d'une image à la
// suivante, presque rien ne change. On ne garde donc que la photo actuelle,
// en entier, et pour chaque image passée la DIFFÉRENCE avec la suivante :
//   différence = photo(n) XOR photo(n-1)
// Les octets identiques donnent 0 : la différence est presque entièrement
// faite de zéros, qu'on compresse en comptant leurs suites. Pour reculer :
//   photo(n-1) = photo(n) XOR différence
// (le XOR, "ou exclusif", s'annule quand on l'applique deux fois).

#include <cstddef>
#include <deque>
#include <string>

class Rewind {
public:
    explicit Rewind(size_t budgetBytes = 256u * 1024 * 1024) : m_budget(budgetBytes) {}

    // Oublie tout (nouveau jeu, sauvegarde rechargée...).
    void reset();
    // Ajoute la photo de l'image qui vient d'être calculée.
    void push(const std::string& state);
    // Recule d'une image : renvoie la photo précédente (false s'il n'y en a plus).
    bool pop(std::string& out);

    size_t frames() const { return m_deltas.size(); }
    size_t bytes() const { return m_bytes; }

private:
    std::string m_current;            // la photo la plus récente, en entier
    std::deque<std::string> m_deltas; // différences compressées, de la plus ancienne à la plus récente
    size_t m_bytes = 0;               // mémoire occupée par les différences
    size_t m_budget;
};
