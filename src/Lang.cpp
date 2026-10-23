#include "Lang.hpp"

#include "Utf8.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace lang {

namespace {

// [C++20] Une table qu'on peut interroger avec un string_view sans créer de
// std::string à chaque fois : il faut un "hasher" et une comparaison dits
// "transparents" (is_transparent).
struct Hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using Table = std::unordered_map<std::string, std::string, Hash, std::equal_to<>>;

struct Pattern {
    std::vector<std::string> parts; // le texte français coupé aux "{}" / "{n}"
    std::vector<bool> numeric;      // pour chaque partie variable : un nombre seulement ("{n}") ?
    std::string translation;
};

std::string g_code = "fr";
Table g_exact;                  // textes sans partie variable
std::vector<Pattern> g_patterns; // textes avec "{}"
Table g_cache;                  // résultats déjà calculés

std::string unescape(std::string s)
{
    for (size_t at; (at = s.find("\\n")) != std::string::npos;)
        s.replace(at, 2, "\n");
    return s;
}

// "{}" : n'importe quel texte ; "{n}" : un nombre seulement ("{n} jeux"
// ne doit pas attraper "Ajoutez vos jeux").
Pattern split(const std::string& text)
{
    Pattern p;
    size_t start = 0;
    for (size_t at = 0; (at = text.find('{', at)) != std::string::npos;) {
        const bool any = text.compare(at, 2, "{}") == 0;
        const bool number = text.compare(at, 3, "{n}") == 0;
        if (!any && !number) {
            ++at;
            continue;
        }
        p.parts.push_back(text.substr(start, at - start));
        p.numeric.push_back(number);
        at += number ? 3 : 2;
        start = at;
    }
    p.parts.push_back(text.substr(start));
    return p;
}

bool isNumber(std::string_view s)
{
    for (const char c : s)
        if (!(c >= '0' && c <= '9') && c != '.' && c != ',' && c != ' ')
            return false;
    return true;
}

// "Il y a 3 min" correspond-il à "Il y a {} min" ? Si oui, remplit captures.
bool match(const Pattern& p, std::string_view text, std::vector<std::string>& captures)
{
    const std::vector<std::string>& parts = p.parts;
    captures.clear();
    if (!text.starts_with(parts.front()))
        return false;
    size_t pos = parts.front().size();
    for (size_t i = 1; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        size_t at;
        if (last) {
            // Le dernier morceau doit finir le texte.
            if (text.size() < pos + parts[i].size() || !text.ends_with(parts[i]))
                return false;
            at = text.size() - parts[i].size();
        } else {
            at = parts[i].empty() ? pos : text.find(parts[i], pos);
            if (at == std::string_view::npos)
                return false;
        }
        if (at == pos)
            return false; // une partie variable n'est jamais vide
        const std::string_view captured = text.substr(pos, at - pos);
        if (p.numeric[i - 1] && !isNumber(captured))
            return false;
        captures.emplace_back(captured);
        pos = at + parts[i].size();
    }
    return true;
}

// depth : les parties variables sont elles-mêmes traduites (2 niveaux au plus).
std::string translate(std::string_view text, int depth = 0)
{
    if (const auto it = g_exact.find(text); it != g_exact.end())
        return it->second;
    std::vector<std::string> captures;
    for (const Pattern& p : g_patterns) {
        if (!match(p, text, captures))
            continue;
        // Les parties variables sont traduites elles aussi si on les connaît
        // ("Vous : Prêt" -> "You: Ready", "Modifiée Il y a 3 min" -> "Modified 3 min ago").
        std::string out;
        size_t start = 0, n = 0;
        for (size_t at; (at = p.translation.find("{}", start)) != std::string::npos; start = at + 2) {
            out += p.translation.substr(start, at - start);
            if (n < captures.size()) {
                out += depth < 2 ? translate(captures[n], depth + 1) : captures[n];
                ++n;
            }
        }
        out += p.translation.substr(start);
        return out;
    }
    return std::string(text);
}

} // namespace

std::vector<Language> available(const fs::path& dir)
{
    std::vector<Language> list = {{"fr", "Français"}};
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".txt")
            continue;
        Language l{toUtf8(it->path().stem()), toUtf8(it->path().stem())};
        // Première ligne : "# name: English"
        std::ifstream in(it->path());
        std::string first;
        if (std::getline(in, first) && first.rfind("# name:", 0) == 0) {
            l.name = first.substr(7);
            l.name.erase(0, l.name.find_first_not_of(' '));
            if (!l.name.empty() && l.name.back() == '\r')
                l.name.pop_back();
        }
        list.push_back(std::move(l));
    }
    return list;
}

void setLanguage(const fs::path& dir, const std::string& code)
{
    g_code = code;
    g_exact.clear();
    g_patterns.clear();
    g_cache.clear();
    if (code == "fr")
        return; // la langue du code : rien à traduire
    std::ifstream in(dir / fromUtf8(code + ".txt"));
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t arrow = line.find(" => ");
        if (line.empty() || line[0] == '#' || arrow == std::string::npos)
            continue;
        std::string from = unescape(line.substr(0, arrow));
        std::string to = unescape(line.substr(arrow + 4));
        ++count;
        if (from.find("{}") != std::string::npos || from.find("{n}") != std::string::npos) {
            Pattern p = split(from);
            for (size_t at; (at = to.find("{n}")) != std::string::npos;)
                to.replace(at, 3, "{}");
            p.translation = std::move(to);
            g_patterns.push_back(std::move(p));
        }
        else
            g_exact[std::move(from)] = std::move(to);
    }
    // Les modèles les plus précis d'abord ("{} jeux trouvés" avant "{} jeux").
    std::sort(g_patterns.begin(), g_patterns.end(), [](const Pattern& a, const Pattern& b) {
        size_t la = 0, lb = 0;
        for (const std::string& p : a.parts)
            la += p.size();
        for (const std::string& p : b.parts)
            lb += p.size();
        return la > lb;
    });
    SDL_Log("Langue %s : %d traductions", code.c_str(), count);
}

void newFrame()
{
    // Les textes qui changent sans cesse (minuteries...) rempliraient le
    // cache : on le vide de temps en temps, entre deux images (jamais
    // pendant qu'un texte traduit est en cours d'utilisation).
    if (g_cache.size() > 20000)
        g_cache.clear();
}

const std::string& current()
{
    return g_code;
}

std::string_view tr(std::string_view text)
{
    if (g_code == "fr" || text.empty())
        return text;
    if (const auto it = g_cache.find(text); it != g_cache.end())
        return it->second;
    std::string result = translate(text);
    // Pour compléter une traduction : EMU_LANG_MISSING=fichier.txt note les
    // textes affichés tels quels (non traduits) qui ont l'air français.
    if (result == text) {
        if (const char* file = SDL_getenv("EMU_LANG_MISSING")) {
            static const char* const french[] = {"é", "è", "à", "ç", "ê", "ô", "û", " le ", " la ", " les ", " de ",
                                                 " des ", " du ", " un ", " une ", " et ", " vos ", " votre "};
            for (const char* f : french) {
                if (text.find(f) != std::string_view::npos) {
                    std::ofstream(fromUtf8(file), std::ios::app) << text << '\n';
                    break;
                }
            }
        }
    }
    return g_cache.emplace(std::string(text), std::move(result)).first->second;
}

} // namespace lang
