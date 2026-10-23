#include "Cheats.hpp"

#include <fstream>
#include <map>
#include <sstream>

std::vector<Cheat> loadCheats(const std::filesystem::path& file)
{
    std::vector<Cheat> cheats;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t a = line.find('\t');
        const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos)
            continue;
        cheats.push_back({line.substr(0, a) == "1", line.substr(a + 1, b - a - 1), line.substr(b + 1)});
    }
    return cheats;
}

void saveCheats(const std::filesystem::path& file, const std::vector<Cheat>& cheats)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    auto clean = [](std::string s) {
        // Une tabulation ou un retour à la ligne casserait le fichier.
        for (char& c : s)
            if (c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        return s;
    };
    for (const Cheat& c : cheats)
        out << (c.enabled ? 1 : 0) << '\t' << clean(c.name) << '\t' << clean(c.code) << '\n';
}

std::vector<Cheat> parseCht(const std::string& text)
{
    // Lignes "clé = valeur" ; les valeurs sont souvent entre guillemets.
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        auto trim = [](std::string s) {
            s.erase(0, s.find_first_not_of(" \t\r\""));
            s.erase(s.find_last_not_of(" \t\r\"") + 1);
            return s;
        };
        values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    std::vector<Cheat> cheats;
    const int count = std::atoi(values["cheats"].c_str());
    for (int i = 0; i < count; ++i) {
        const std::string prefix = "cheat" + std::to_string(i) + "_";
        Cheat c;
        c.name = values[prefix + "desc"];
        c.code = values[prefix + "code"];
        if (c.name.empty())
            c.name = "Code " + std::to_string(i + 1);
        if (!c.code.empty())
            cheats.push_back(std::move(c));
    }
    return cheats;
}
