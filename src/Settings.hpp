#pragma once
// Settings : les réglages de l'écran Paramètres, enregistrés dans
// settings.ini (une ligne "nom=valeur" par réglage).

#include <filesystem>
#include <map>
#include <string>

struct Settings {
    // Affichage
    std::string language = "en"; // langue de l'interface (voir Lang.hpp) : anglais par défaut
    bool fullscreenOnLaunch = false;
    int scaling = 0;      // 0 = pixels nets, 1 = lissé
    int aspect = 0;       // 0 = d'origine, 1 = 4:3, 2 = 16:9
    bool showFps = false;
    bool preferVulkan = false; // cores 3D qui savent faire les deux : Vulkan plutôt qu'OpenGL
    // Son
    int audioLatency = 1; // 0 = basse, 1 = normale, 2 = sûre
    int volume = 100;     // 0 à 100 %
    // Manettes
    int menuButton = 0;   // 0 = Select + Start, 1 = bouton central
    bool keyboardSeparate = false; // le clavier est un joueur à part (manettes = joueurs 2, 3...)
    bool wiiMousePointer = false;  // Wii : viser avec la souris (sinon au stick droit)
    bool rewind = true;            // retour en arrière (Retour arrière, ou Select + L)
    int fastForwardSpeed = 4;      // avance rapide : x2, x3, x4 ou x8 (Tab, ou Select + R)
    // Sauvegardes
    bool autoSaveState = true;
    // Jaquettes et informations
    bool autoScrape = true; // chercher tout seul les nouveaux jeux
    // Jeu en ligne
    std::string nickname = "Joueur";
    // Émulateurs externes (Switch, Xbox, PS3...) : console -> programme, et
    // console -> arguments ({rom} = le jeu). Vide = valeurs par défaut.
    std::map<std::string, std::string> externalPaths;
    std::map<std::string, std::string> externalArgs;

    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;

    // Durée de son maximale en attente, selon la latence choisie.
    double maxAudioQueue() const;
};
