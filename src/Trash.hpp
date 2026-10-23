#pragma once
// Trash : envoyer des fichiers à la Corbeille de Windows.
//
// Plutôt que de les effacer pour de bon : une erreur de clic se rattrape en
// restaurant le fichier depuis la Corbeille.

#include <filesystem>
#include <string>
#include <vector>

// Renvoie false (avec une explication dans error) si un fichier n'a pas pu
// être déplacé.
bool moveToTrash(const std::vector<std::filesystem::path>& files, std::string& error);
