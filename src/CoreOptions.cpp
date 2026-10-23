#include "CoreOptions.hpp"

#include <fstream>

std::map<std::string, std::string> readCoreOptions(const std::filesystem::path& file)
{
    std::map<std::string, std::string> options;
    std::ifstream in(file);
    std::string line;
    auto trim = [](std::string s) {
        s.erase(0, s.find_first_not_of(" \t\""));
        s.erase(s.find_last_not_of(" \t\"") + 1);
        return s;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos)
            continue;
        options[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return options;
}

namespace {

// Format : une option par ligne, champs séparés par des tabulations :
//   clé  libellé  explication  défaut  valeur1  libellé1  valeur2  libellé2...
// Les tabulations et retours à la ligne des textes sont échappés (\t, \n).
std::string escape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\\') out += "\\\\";
        else if (c != '\r') out += c;
    }
    return out;
}

std::string unescape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

void saveOptionList(const std::filesystem::path& file, const std::vector<CoreOption>& options)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    for (const CoreOption& o : options) {
        out << escape(o.key) << '\t' << escape(o.label) << '\t' << escape(o.info) << '\t' << escape(o.defaultValue);
        for (const auto& [value, label] : o.values)
            out << '\t' << escape(value) << '\t' << escape(label);
        out << '\n';
    }
}

std::vector<CoreOption> loadOptionList(const std::filesystem::path& file)
{
    std::vector<CoreOption> options;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t start = 0;
        for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1)
            f.push_back(unescape(line.substr(start, tab - start)));
        f.push_back(unescape(line.substr(start)));
        if (f.size() < 4)
            continue;
        CoreOption o{f[0], f[1], f[2], {}, f[3]};
        for (size_t i = 4; i + 1 < f.size(); i += 2)
            o.values.emplace_back(f[i], f[i + 1]);
        options.push_back(std::move(o));
    }
    return options;
}

void writeCoreOptions(const std::filesystem::path& file, const std::map<std::string, std::string>& options)
{
    std::ofstream out(file, std::ios::trunc);
    out << "# Options des cores modifiées (Paramètres > Options des cores).\n"
           "# Une par ligne : nom=valeur. Les autres gardent leur valeur par défaut.\n";
    for (const auto& [key, value] : options)
        out << key << '=' << value << '\n';
}
