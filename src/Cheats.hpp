#pragma once
// Cheats : les codes de triche (Game Genie, Action Replay, GameShark...).
//
// L'émulateur ne les comprend pas lui-même : il les donne au core
// (retro_cheat_set), qui sait les appliquer à sa console. Chaque jeu a sa
// liste dans cheats/<nom du fichier>.txt, une ligne par code :
//
//   <actif 0/1> TAB <nom> TAB <code>
//
// La base libretro propose des milliers de codes tout prêts, au format
// RetroArch (.cht) : parseCht() sait les lire.

#include <filesystem>
#include <string>
#include <vector>

struct Cheat {
    bool enabled = false;
    std::string name; // "Vies infinies"
    std::string code; // "7E0DBF63", plusieurs codes séparés par "+"
};

std::vector<Cheat> loadCheats(const std::filesystem::path& file);
void saveCheats(const std::filesystem::path& file, const std::vector<Cheat>& cheats);
// Le format .cht de RetroArch : cheat0_desc = "...", cheat0_code = "...".
std::vector<Cheat> parseCht(const std::string& text);
