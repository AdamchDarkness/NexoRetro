#pragma once
// ExternalSetup : les fichiers système des émulateurs externes.
//
//   PS3 (RPCS3)    : le firmware officiel PS3UPDAT.PUP, que Sony distribue
//                    gratuitement sur son site ; RPCS3 l'installe lui-même.
//   Switch         : prod.keys (et title.keys), extraites de VOTRE console.
//   Xbox (xemu)    : le BIOS (MCPX + flash) de VOTRE console, et l'image du
//                    disque dur (proposée vierge par xemu).
//   PS4, Xbox 360  : rien d'obligatoire.
//
// On ne télécharge jamais ces fichiers à la place de l'utilisateur : on les
// range au bon endroit et on règle l'émulateur pour qu'il les trouve.

#include <filesystem>
#include <string>
#include <vector>

namespace external {

struct SystemFiles {
    bool needed = false;   // cette console a-t-elle besoin de fichiers système ?
    bool ready = false;    // ... et sont-ils en place ?
    std::string label;     // "Firmware PS3", "Clés de la Switch"...
    std::string status;    // "Installé", "Manquant : prod.keys"...
    const char* help = ""; // d'où viennent ces fichiers
    const char* officialPage = nullptr; // page officielle utile (firmware Sony, disque xemu)
    const char* officialLabel = nullptr;
};

// exe : l'émulateur (vide s'il n'est pas installé). biosDir : bios/.
SystemFiles systemFiles(const std::string& systemId, const std::filesystem::path& exe, const std::filesystem::path& biosDir);

// Range les fichiers choisis par l'utilisateur et règle l'émulateur.
// Renvoie un message pour l'utilisateur. (PS3 : lance RPCS3 qui installe
// le firmware dans sa propre fenêtre.)
std::string importSystemFiles(const std::string& systemId, const std::filesystem::path& exe,
                              const std::filesystem::path& biosDir, const std::vector<std::filesystem::path>& files);

// Règle l'émulateur pour qu'il trouve les fichiers déjà rangés (après une
// installation ou une mise à jour de l'émulateur).
void configure(const std::string& systemId, const std::filesystem::path& exe, const std::filesystem::path& biosDir);

} // namespace external
