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
    // Bibliothèque : 0 = grille, 1 = liste, 2 = carrousel
    int libraryView = 0;
    // Émulateurs (cores) : chercher les mises à jour au démarrage (une fois par jour)
    bool coreAutoUpdate = true;
    // Jeu en ligne
    std::string nickname = "Joueur";
    bool netStats = false;      // ping et pertes affichés pendant la partie
    int voiceMode = 1;          // chat vocal : 0 = désactivé, 1 = micro ouvert, 2 = appuyer pour parler
    int voiceKey = 23;          // touche "appuyer pour parler" (code SDL : 23 = T)
    int voiceVolume = 100;      // volume des voix des autres, 0 à 200 %
    int voiceSensitivity = 50;  // micro ouvert : 0 = il faut parler fort, 100 = le moindre bruit passe
    bool discordPresence = true; // "Joue à Zelda sur SNES" sur Discord
    // L'identifiant de l'application Discord "Émulateur" (public, pas un
    // secret : il dit juste à Discord quel nom afficher). Vide = celui
    // fourni avec l'émulateur (kDiscordAppId, Discord.hpp).
    std::string discordAppId;
    // Émulateurs externes (Switch, Xbox, PS3...) : console -> programme, et
    // console -> arguments ({rom} = le jeu). Vide = valeurs par défaut.
    std::map<std::string, std::string> externalPaths;
    std::map<std::string, std::string> externalArgs;

    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;

    // Durée de son maximale en attente, selon la latence choisie.
    double maxAudioQueue() const;
};
