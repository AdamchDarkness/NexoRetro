#pragma once
// SaveFiles : où sont rangées les sauvegardes d'un jeu.
//
// Chaque jeu a son dossier, saves/<nom du fichier du jeu>/, avec :
//   <nom>.srm         la sauvegarde du jeu (la "pile" de la cartouche)
//   <nom>.state       la sauvegarde d'état rapide (F2)
//   <nom>.auto.state  la sauvegarde d'état automatique (en quittant)
//   les captures d'écran (F12)
//
// Les anciennes versions rangeaient tout en vrac dans saves/ : la première
// fois qu'on demande les fichiers d'un jeu, les siens sont déplacés dans son
// dossier. (Les fichiers propres aux cores, comme les cartes mémoire de la
// PS2, restent dans saves/ : le core les y cherche.)

#include <filesystem>
#include <string>

struct SaveFiles {
    std::filesystem::path dir;
    std::filesystem::path sram;
    std::filesystem::path state;
    std::filesystem::path autoState;
};

// stem : le nom du fichier du jeu, sans extension ("Super Mario World (USA)").
SaveFiles saveFilesFor(const std::filesystem::path& savesDir, const std::string& stem);
