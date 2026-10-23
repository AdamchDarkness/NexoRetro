#include "GameDatabase.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace scrape {

// --- Comparer des noms --------------------------------------------------

namespace {

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Les chiffres romains les plus courants dans les titres.
const char* romanToDigits(const std::string& word)
{
    static const char* const roman[] = {"ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii", "xiii"};
    static const char* const digits[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"};
    for (size_t i = 0; i < std::size(roman); ++i)
        if (word == roman[i])
            return digits[i];
    return nullptr;
}

// Coupe "Super Mario World 2 - Yoshi's Island" en "Super Mario World 2".
std::string_view mainPart(std::string_view name)
{
    size_t cut = name.find(" - ");
    cut = std::min(cut, name.find(": "));
    cut = std::min(cut, name.find(" ("));
    return cut == std::string_view::npos ? name : name.substr(0, cut);
}

// "Zelda III - A Link to the Past" -> "A Link to the Past" (vide si pas de sous-titre).
std::string_view subtitle(std::string_view name)
{
    const size_t start = name.find(" - ");
    if (start == std::string_view::npos)
        return {};
    std::string_view sub = name.substr(start + 3);
    return sub.substr(0, std::min(sub.find(" - "), sub.find(" (")));
}

// Un sous-titre assez long pour désigner un seul jeu ("A Link to the Past",
// pas "Remix" ou "Part 2").
bool distinctive(const std::string& key)
{
    return std::count(key.begin(), key.end(), ' ') >= 2 && key.size() >= 12;
}

} // namespace

std::string matchKey(std::string_view name)
{
    // 1. On retire ce qui est entre () ou [] : région, version, langues.
    std::string text;
    int depth = 0;
    for (const char c : name) {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        else if (depth == 0)
            text += c;
    }
    // 2. Minuscules ; "&" devient "and" ; toute ponctuation devient un espace.
    //    Les octets >= 128 (accents en UTF-8) sont gardés tels quels.
    std::string spaced;
    for (const char c : lower(text)) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\'')
            continue; // "Yoshi's" = "Yoshis"
        else if (std::isalnum(u) || u >= 128)
            spaced += c;
        else
            spaced += ' ';
    }
    // 3. Mot par mot : on oublie "the" et "and" (les images libretro écrivent
    //    "Mario _ Luigi" pour "Mario & Luigi"), on écrit les numéros en chiffres.
    std::istringstream words(spaced);
    std::string word, key;
    while (words >> word) {
        if (word == "the" || word == "and")
            continue;
        if (const char* digits = romanToDigits(word))
            word = digits;
        if (!key.empty())
            key += ' ';
        key += word;
    }
    return key;
}

std::string displayName(std::string_view officialName)
{
    // "A / B" : deux titres (souvent Japon / reste du monde) : on garde le premier.
    const size_t slash = officialName.find(" / ");
    if (slash != std::string_view::npos)
        officialName = officialName.substr(0, slash);
    // On retire ce qui est entre parenthèses (version, région, date).
    std::string out;
    int depth = 0;
    for (const char c : officialName) {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        else if (depth == 0)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

int versionScore(std::string_view name, std::string_view region)
{
    const std::string n = lower(name);
    int score = 0;
    if (!region.empty() && n.find("(" + lower(region)) != std::string::npos)
        score += 100;
    else if (!region.empty() && n.find(lower(region)) != std::string::npos)
        score += 80; // "(USA, Europe)"
    // Pour un joueur français : France, puis Europe, puis le reste du monde.
    static const std::pair<const char*, int> regions[] = {{"france", 40}, {"europe", 35}, {"world", 30}, {"usa", 25},
                                                          {"japan", 5}};
    for (const auto& [r, points] : regions) {
        if (n.find(r) != std::string::npos) {
            score += points;
            break;
        }
    }
    // Les versions "spéciales" ne doivent servir qu'en dernier recours.
    static const char* const bad[] = {"(beta", "(proto", "(demo", "(sample", "(kiosk", "(pirate", "(unl", "(hack",
                                      "(aftermarket", "virtual console", "switch online", "(program"};
    for (const char* b : bad)
        if (n.find(b) != std::string::npos)
            score -= 60;
    if (n.find("(rev") != std::string::npos)
        score -= 1; // à égalité, la version d'origine
    return score;
}

void NameIndex::add(const std::string& name)
{
    const size_t i = m_names.size();
    m_names.push_back(name);
    m_byKey.emplace(matchKey(name), i);
    m_byMain.emplace(matchKey(mainPart(name)), i);
    if (const std::string sub = matchKey(subtitle(name)); distinctive(sub))
        m_bySub.emplace(sub, i);
}

std::optional<size_t> NameIndex::best(const std::vector<size_t>& candidates, std::string_view region) const
{
    if (candidates.empty())
        return std::nullopt;
    return *std::max_element(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
        const int sa = versionScore(m_names[a], region);
        const int sb = versionScore(m_names[b], region);
        // À score égal, le nom le plus court (le moins de mentions en plus).
        return sa != sb ? sa < sb : m_names[a].size() > m_names[b].size();
    });
}

std::optional<size_t> NameIndex::find(std::string_view title, std::string_view region) const
{
    // [C++] equal_range renvoie la paire [début, fin) des éléments ayant
    // cette clé dans une multimap (plusieurs valeurs par clé).
    auto collect = [](const std::unordered_multimap<std::string, size_t>& map, const std::string& key) {
        std::vector<size_t> out;
        const auto [first, last] = map.equal_range(key);
        for (auto it = first; it != last; ++it)
            out.push_back(it->second);
        return out;
    };
    const std::string key = matchKey(title);
    if (key.empty())
        return std::nullopt;
    // 1. Le nom exact.
    if (auto found = best(collect(m_byKey, key), region))
        return found;
    // 2. Sans les sous-titres : "Zelda III" retrouve "Zelda III - ..." et inversement.
    const std::string main = matchKey(mainPart(title));
    if (auto found = best(collect(m_byKey, main), region))
        return found;
    if (auto found = best(collect(m_byMain, main), region))
        return found;
    // 3. Par le sous-titre seul : "Zelda III - A Link to the Past" retrouve
    //    "Legend of Zelda, The - A Link to the Past".
    if (const std::string sub = matchKey(subtitle(title)); distinctive(sub))
        return best(collect(m_bySub, sub), region);
    return std::nullopt;
}

std::vector<size_t> NameIndex::search(std::string_view query, std::string_view region, size_t max) const
{
    std::vector<std::string> words;
    {
        std::istringstream in(matchKey(query));
        std::string w;
        while (in >> w)
            words.push_back(w);
    }
    if (words.empty())
        return {};
    const std::string key = matchKey(query);
    std::vector<std::pair<int, size_t>> scored;
    for (size_t i = 0; i < m_names.size(); ++i) {
        const std::string k = matchKey(m_names[i]);
        const std::string padded = " " + k + " ";
        const bool all = std::all_of(words.begin(), words.end(),
                                     [&](const std::string& w) { return padded.find(" " + w + " ") != std::string::npos; });
        if (!all)
            continue;
        int score = versionScore(m_names[i], region);
        if (k == key)
            score += 1000; // le titre exact en premier
        score -= static_cast<int>(k.size()); // puis les titres les plus courts
        scored.emplace_back(score, i);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<size_t> out;
    for (size_t i = 0; i < scored.size() && out.size() < max; ++i)
        out.push_back(scored[i].second);
    return out;
}

// --- Lire le .rdb (MessagePack) ------------------------------------------

namespace {

// Une valeur MessagePack, réduite à ce dont on a besoin.
struct Value {
    enum class Type { Nil, Int, String, Map, Other } type = Type::Other;
    long long number = 0;
    std::string text;   // chaîne ou octets bruts
    size_t entries = 0; // nombre de paires (pour une Map)
};

class Reader {
public:
    Reader(const std::string& data, size_t pos) : m_data(data), m_pos(pos) {}

    bool done() const { return m_pos >= m_data.size(); }

    // Lit l'en-tête d'une valeur. Pour une Map, ses éléments suivent : on
    // les lit ensuite un par un (clé, valeur, clé, valeur...).
    bool read(Value& v)
    {
        uint8_t b = 0;
        if (!byte(b))
            return false;
        v = Value{};
        if (b <= 0x7F) { v.type = Value::Type::Int; v.number = b; return true; }
        if (b >= 0xE0) { v.type = Value::Type::Int; v.number = static_cast<int8_t>(b); return true; }
        if ((b & 0xF0) == 0x80) { v.type = Value::Type::Map; v.entries = b & 0x0F; return true; }
        if ((b & 0xF0) == 0x90) return skipItems(b & 0x0F); // tableau : ignoré
        if ((b & 0xE0) == 0xA0) { v.type = Value::Type::String; return bytes(b & 0x1F, v.text); }
        switch (b) {
        case 0xC0: v.type = Value::Type::Nil; return true;
        case 0xC2: case 0xC3: v.type = Value::Type::Int; v.number = b == 0xC3; return true;
        case 0xC4: case 0xD9: return sized(1, v);
        case 0xC5: case 0xDA: return sized(2, v);
        case 0xC6: case 0xDB: return sized(4, v);
        case 0xCC: return unsignedInt(1, v);
        case 0xCD: return unsignedInt(2, v);
        case 0xCE: return unsignedInt(4, v);
        case 0xCF: return unsignedInt(8, v);
        case 0xD0: if (!unsignedInt(1, v)) return false; v.number = static_cast<int8_t>(v.number); return true;
        case 0xD1: if (!unsignedInt(2, v)) return false; v.number = static_cast<int16_t>(v.number); return true;
        case 0xD2: if (!unsignedInt(4, v)) return false; v.number = static_cast<int32_t>(v.number); return true;
        case 0xD3: return unsignedInt(8, v);
        case 0xDE: { uint64_t n; if (!bigEndian(2, n)) return false; v.type = Value::Type::Map; v.entries = n; return true; }
        case 0xDF: { uint64_t n; if (!bigEndian(4, n)) return false; v.type = Value::Type::Map; v.entries = n; return true; }
        case 0xDC: { uint64_t n; return bigEndian(2, n) && skipItems(n); }
        case 0xDD: { uint64_t n; return bigEndian(4, n) && skipItems(n); }
        default: return false; // type inconnu : fichier abîmé
        }
    }

    bool skipItems(uint64_t n)
    {
        for (uint64_t i = 0; i < n; ++i) {
            Value v;
            if (!read(v))
                return false;
            if (v.type == Value::Type::Map && !skipItems(v.entries * 2))
                return false;
        }
        return true;
    }

private:
    bool byte(uint8_t& b)
    {
        if (m_pos >= m_data.size())
            return false;
        b = static_cast<uint8_t>(m_data[m_pos++]);
        return true;
    }
    // Les entiers MessagePack sont "big-endian" : octet de poids fort d'abord.
    bool bigEndian(int size, uint64_t& out)
    {
        out = 0;
        for (int i = 0; i < size; ++i) {
            uint8_t b = 0;
            if (!byte(b))
                return false;
            out = (out << 8) | b;
        }
        return true;
    }
    bool unsignedInt(int size, Value& v)
    {
        uint64_t n = 0;
        if (!bigEndian(size, n))
            return false;
        v.type = Value::Type::Int;
        v.number = static_cast<long long>(n);
        return true;
    }
    bool bytes(uint64_t n, std::string& out)
    {
        if (n > m_data.size() - m_pos)
            return false;
        out.assign(m_data, m_pos, static_cast<size_t>(n));
        m_pos += static_cast<size_t>(n);
        return true;
    }
    bool sized(int lengthSize, Value& v)
    {
        uint64_t n = 0;
        v.type = Value::Type::String;
        return bigEndian(lengthSize, n) && bytes(n, v.text);
    }

    const std::string& m_data;
    size_t m_pos;
};

std::string asText(const Value& v)
{
    if (v.type == Value::Type::String)
        return v.text;
    if (v.type == Value::Type::Int)
        return std::to_string(v.number);
    return {};
}

void fillIfEmpty(std::string& field, const std::string& value)
{
    if (field.empty())
        field = value;
}

} // namespace

bool GameDatabase::load(const std::string& bytes)
{
    m_games.clear();
    m_byCrc.clear();
    m_byFile.clear();
    m_names = NameIndex();
    // En-tête : "RARCHDB\0" puis 8 octets (position des métadonnées).
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "RARCHDB", 7) != 0)
        return false;

    Reader reader(bytes, 16);
    Value map;
    while (!reader.done() && reader.read(map)) {
        if (map.type != Value::Type::Map)
            break; // fin des jeux (nil) : les métadonnées suivent
        DbGame g;
        for (size_t i = 0; i < map.entries; ++i) {
            Value key, value;
            if (!reader.read(key) || !reader.read(value))
                return !m_games.empty();
            if (value.type == Value::Type::Map) {
                // Valeur imbriquée (n'existe pas dans ces bases) : on la saute.
                if (!reader.skipItems(value.entries * 2))
                    return !m_games.empty();
                continue;
            }
            const std::string& k = key.text;
            if (k == "name") g.name = value.text;
            else if (k == "genre") g.genre = asText(value);
            else if (k == "developer") g.developer = asText(value);
            else if (k == "publisher") g.publisher = asText(value);
            else if (k == "franchise") g.franchise = asText(value);
            else if (k == "releaseyear") g.year = static_cast<int>(value.number);
            else if (k == "releasemonth") g.month = static_cast<int>(value.number);
            else if (k == "users") g.players = static_cast<int>(value.number);
            else if (k == "rom_name") g.fileName = lower(value.text);
            else if (k == "crc" && value.text.size() == 4) {
                // Le CRC est rangé en 4 octets bruts, poids fort d'abord.
                const auto* c = reinterpret_cast<const unsigned char*>(value.text.data());
                g.crc = (uint32_t(c[0]) << 24) | (uint32_t(c[1]) << 16) | (uint32_t(c[2]) << 8) | c[3];
            }
        }
        if (g.crc == 0 && g.name.empty())
            continue;
        // Un même jeu apparaît plusieurs fois (une fois par liste d'origine :
        // No-Intro, liste des genres, des développeurs...). On fusionne.
        if (g.crc != 0) {
            const auto it = m_byCrc.find(g.crc);
            if (it != m_byCrc.end()) {
                DbGame& existing = m_games[it->second];
                fillIfEmpty(existing.name, g.name);
                fillIfEmpty(existing.fileName, g.fileName);
                fillIfEmpty(existing.genre, g.genre);
                fillIfEmpty(existing.developer, g.developer);
                fillIfEmpty(existing.publisher, g.publisher);
                fillIfEmpty(existing.franchise, g.franchise);
                if (!existing.year) existing.year = g.year;
                if (!existing.month) existing.month = g.month;
                if (!existing.players) existing.players = g.players;
                continue;
            }
            m_byCrc[g.crc] = m_games.size();
        }
        m_games.push_back(std::move(g));
    }
    // Deux fichiers différents peuvent porter le même nom (ex. la version
    // "SNES Classic" d'un jeu) : ils partagent alors les mêmes informations.
    std::unordered_map<std::string, size_t> richest;
    auto richness = [](const DbGame& g) { return !g.genre.empty() + !g.developer.empty() + !g.publisher.empty() + (g.year > 0); };
    for (size_t i = 0; i < m_games.size(); ++i) {
        auto [it, inserted] = richest.emplace(m_games[i].name, i);
        if (!inserted && richness(m_games[i]) > richness(m_games[it->second]))
            it->second = i;
    }
    for (DbGame& g : m_games) {
        const DbGame& r = m_games[richest[g.name]];
        if (&r == &g)
            continue;
        fillIfEmpty(g.genre, r.genre);
        fillIfEmpty(g.developer, r.developer);
        fillIfEmpty(g.publisher, r.publisher);
        fillIfEmpty(g.franchise, r.franchise);
        if (!g.year) g.year = r.year;
        if (!g.month) g.month = r.month;
        if (!g.players) g.players = r.players;
    }
    // Les index sont construits à la fin (les fusions ne changent pas les noms).
    // Plusieurs jeux peuvent annoncer le même nom de fichier : les "hacks"
    // (jeux modifiés par des fans) gardent celui du jeu d'origine. On garde
    // celui dont le nom officiel correspond au fichier, jamais un hack s'il
    // y a mieux.
    auto fileScore = [](const DbGame& g) {
        const std::string stem = g.fileName.substr(0, g.fileName.rfind('.'));
        int score = 0;
        if (matchKey(g.name) == matchKey(stem))
            score += 2;
        const std::string n = lower(g.name);
        if (n.find("hack") == std::string::npos && n.find("[t-") == std::string::npos)
            score += 1;
        return score;
    };
    for (size_t i = 0; i < m_games.size(); ++i) {
        m_names.add(m_games[i].name);
        if (m_games[i].fileName.empty())
            continue;
        auto [it, inserted] = m_byFile.emplace(m_games[i].fileName, i);
        if (!inserted && fileScore(m_games[i]) > fileScore(m_games[it->second]))
            it->second = i;
    }
    return !m_games.empty();
}

const DbGame* GameDatabase::byCrc(uint32_t crc) const
{
    const auto it = m_byCrc.find(crc);
    return it == m_byCrc.end() ? nullptr : &m_games[it->second];
}

const DbGame* GameDatabase::byFileName(std::string_view fileName) const
{
    const auto it = m_byFile.find(lower(fileName));
    return it == m_byFile.end() ? nullptr : &m_games[it->second];
}

const DbGame* GameDatabase::byTitle(std::string_view title, std::string_view region) const
{
    const std::optional<size_t> i = m_names.find(title, region);
    return i ? &m_games[*i] : nullptr;
}

} // namespace scrape
