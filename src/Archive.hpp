#pragma once
// Archive : créer et décompresser des .zip avec le tar.exe fourni par
// Windows (depuis Windows 10), lancé sans fenêtre. Pas de bibliothèque à
// ajouter : tar sait lire et écrire les .zip (et lire les .7z).

#include <filesystem>

namespace archive {

// Décompresse "file" dans le dossier "dest" (créé au besoin).
bool extract(const std::filesystem::path& file, const std::filesystem::path& dest);

// Crée le .zip "file" avec tout le contenu du dossier "folder" (ses
// fichiers et sous-dossiers, sans le dossier lui-même).
bool zipFolder(const std::filesystem::path& folder, const std::filesystem::path& file);

} // namespace archive
