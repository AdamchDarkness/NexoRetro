#pragma once
// CoreOptions : le fichier core-options.txt, où sont rangées les options de
// cores choisies par l'utilisateur, une par ligne :
//
//   # commentaire
//   mame_softlists_enable=enabled
//
// Les noms commencent par celui du core ("mame_", "mgba_"...) : un seul
// fichier suffit pour tous. Fichier facultatif : sans lui, chaque core garde
// ses valeurs par défaut. L'écran Paramètres → Options des cores l'écrit.

#include "Core.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

std::map<std::string, std::string> readCoreOptions(const std::filesystem::path& file);
void writeCoreOptions(const std::filesystem::path& file, const std::map<std::string, std::string>& options);

// La description des options d'un core, gardée sur le disque. Certains
// cores (FBNeo...) n'annoncent leurs options qu'une fois un jeu chargé :
// on retient celles vues au dernier lancement pour l'écran des options.
void saveOptionList(const std::filesystem::path& file, const std::vector<CoreOption>& options);
std::vector<CoreOption> loadOptionList(const std::filesystem::path& file);
