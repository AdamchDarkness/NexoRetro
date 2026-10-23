#pragma once
// UiSounds : les petits sons de l'interface (clic, survol, validation,
// retour, notification). Seulement l'interface : jamais pendant un jeu.
//
// Aucun fichier audio : chaque son est CALCULÉ au démarrage (synthèse), à
// partir d'ondes simples :
//   - sinusoïde : un son pur et doux (pack « Doux ») ;
//   - carrée : le son des consoles 8 bits (pack « 8 bits ») ;
//   - dents de scie avec glissement de hauteur : les bornes d'arcade.
// Ils passent par leur propre flux audio SDL, à côté de celui des jeux :
// SDL mélange les deux, le son du jeu n'est pas touché.

namespace sounds {

enum class Event { Click, Hover, Validate, Back, Notify };

// Les packs de sons (Paramètres > Apparence).
enum class Pack { Soft, Bits, Arcade };
constexpr int kPackCount = 3;

// Calcule tous les sons et ouvre le flux audio. false si pas de carte son
// (l'interface marche alors sans les sons).
bool init();
void shutdown();

// Réglages : activés ou non, volume (0 à 100 %), pack.
void configure(bool enabled, int volume, Pack pack);
// Pendant un jeu : plus aucun son d'interface.
void setSuspended(bool suspended);

void play(Event event);
// Fait entendre un pack (au moment de le choisir), même s'il n'est pas actif.
void preview(Pack pack);

// À appeler à la fin de chaque image d'ImGui : repère tout seul les clics,
// les survols, la navigation au clavier ou à la manette, Entrée et Échap.
void watchImGui();

} // namespace sounds
