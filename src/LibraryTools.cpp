#include "LibraryTools.hpp"

#include "Hash.hpp"
#include "Lang.hpp"
#include "Utf8.hpp"
#include "scrape/Scraper.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Une ligne du tableau, quel que soit le format.
struct Row {
    std::string title, system, region, genre, year, publisher, players, size, favorite, playTime, lastPlayed, file;
};

std::string tr(std::string_view text) { return lang::trLogical(text); }

std::string sizeText(std::uintmax_t bytes)
{
    char text[32];
    if (bytes >= 1024ull * 1024 * 1024)
        std::snprintf(text, sizeof(text), "%.1f Go", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else
        std::snprintf(text, sizeof(text), "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

std::vector<Row> rowsOf(const std::vector<Game>& games, const UserData& userData, const scrape::Scraper& scraper)
{
    std::vector<Row> rows;
    for (const Game& g : games) {
        Row r;
        r.title = g.title;
        r.system = g.system->name;
        r.region = g.region;
        r.size = sizeText(g.size);
        r.file = toUtf8(g.path);
        if (const scrape::GameInfo* info = scraper.find(g.path); info && info->status == scrape::GameInfo::Status::Found) {
            r.genre = info->genre;
            r.publisher = info->publisher;
            r.year = info->year > 0 ? std::to_string(info->year) : "";
            r.players = info->players > 0 ? std::to_string(info->players) : "";
        }
        if (const GameRecord* rec = userData.find(g.path)) {
            r.favorite = rec->favorite ? tr("Oui") : "";
            if (rec->playSeconds > 0) {
                char t[32];
                std::snprintf(t, sizeof(t), "%lld h %02lld", rec->playSeconds / 3600, rec->playSeconds / 60 % 60);
                r.playTime = t;
            }
            if (rec->lastPlayed > 0) {
                const std::time_t when = static_cast<std::time_t>(rec->lastPlayed);
                std::tm day{};
                localtime_s(&day, &when);
                char t[16];
                std::strftime(t, sizeof(t), "%Y-%m-%d", &day);
                r.lastPlayed = t;
            }
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<std::string> headers()
{
    return {tr("Titre"),    tr("Console"),         tr("Région"),         tr("Genre"),
            tr("Année"),    tr("Éditeur"),         tr("Joueurs"),        tr("Taille"),
            tr("Favori"),   tr("Temps de jeu"),    tr("Dernière partie"), tr("Fichier")};
}

std::vector<std::string> fields(const Row& r)
{
    return {r.title, r.system, r.region, r.genre, r.year, r.publisher, r.players, r.size, r.favorite, r.playTime,
            r.lastPlayed, r.file};
}

std::string htmlEscape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string countText(size_t count) { return std::to_string(count) + (count > 1 ? " jeux" : " jeu"); }

std::string exportedText(size_t count, const fs::path& file)
{
    return std::to_string(count) + (count > 1 ? " jeux exportés dans " : " jeu exporté dans ") + toUtf8(file.filename());
}

} // namespace

namespace library_tools {

std::string exportCsv(const std::vector<Game>& games, const UserData& userData, const scrape::Scraper& scraper,
                      const fs::path& file)
{
    // Excel en français, allemand, espagnol... attend des points-virgules
    // (la virgule y sert aux nombres décimaux) ; en anglais, des virgules.
    const std::string& language = lang::current();
    const char sep = (language == "fr" || language == "de" || language == "es" || language == "pt") ? ';' : ',';
    auto cell = [&](const std::string& value) {
        // Entre guillemets si besoin ; un guillemet s'écrit en double.
        if (value.find_first_of(std::string("\"\n") + sep) == std::string::npos)
            return value;
        std::string out = "\"";
        for (const char c : value)
            out += c == '"' ? std::string("\"\"") : std::string(1, c);
        return out + "\"";
    };
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return "Impossible d'écrire " + toUtf8(file.filename());
    out << "\xEF\xBB\xBF"; // "BOM" UTF-8 : sans lui, Excel abîme les accents
    auto line = [&](const std::vector<std::string>& values) {
        for (size_t i = 0; i < values.size(); ++i)
            out << (i ? std::string(1, sep) : std::string()) << cell(values[i]);
        out << "\r\n";
    };
    line(headers());
    for (const Row& r : rowsOf(games, userData, scraper))
        line(fields(r));
    return exportedText(games.size(), file);
}

std::string exportHtml(const std::vector<Game>& games, const UserData& userData, const scrape::Scraper& scraper,
                       const fs::path& file)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return "Impossible d'écrire " + toUtf8(file.filename());
    // Une page autonome (aucun fichier à côté) : un tableau, un champ de
    // recherche, des couleurs claires ou sombres selon le système.
    std::map<std::string, int> perSystem;
    for (const Game& g : games)
        ++perSystem[g.system->name];
    const std::time_t now = std::time(nullptr);
    std::tm day{};
    localtime_s(&day, &now);
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &day);

    out << "<!doctype html>\n<html lang=\"" << lang::current() << "\"" << (lang::rtl() ? " dir=\"rtl\"" : "") << ">\n"
        << "<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << htmlEscape(tr("Ma collection")) << " · Nexo Retro</title>\n<style>\n"
        << ":root{--bg:#f6f6f3;--card:#fff;--ink:#16161d;--muted:#6b6b78;--line:#e7e7ea;--accent:#2f5bff}\n"
        << "@media (prefers-color-scheme:dark){:root{--bg:#111116;--card:#1b1b22;--ink:#f2f2f5;--muted:#9a9aa8;"
           "--line:#2a2a33;--accent:#7c95ff}}\n"
        << "body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,'Segoe UI',sans-serif}\n"
        << "main{max-width:1200px;margin:0 auto;padding:32px 16px}\n"
        << "h1{margin:0 0 4px;font-size:28px}.sub{color:var(--muted);margin:0 0 20px}\n"
        << ".chips{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:16px}"
           ".chip{background:var(--card);border:1px solid var(--line);border-radius:99px;padding:4px 12px;font-size:13px}\n"
        << "input{width:100%;box-sizing:border-box;padding:10px 14px;border-radius:12px;border:1px solid var(--line);"
           "background:var(--card);color:var(--ink);font:inherit;margin-bottom:16px}\n"
        << ".wrap{overflow-x:auto;background:var(--card);border:1px solid var(--line);border-radius:16px}\n"
        << "table{border-collapse:collapse;width:100%}th,td{padding:9px 12px;text-align:start;white-space:nowrap;"
           "border-bottom:1px solid var(--line)}th{font-size:12px;color:var(--muted);text-transform:uppercase}\n"
        << "td.file{color:var(--muted);font-size:12px}tr:last-child td{border-bottom:0}\n"
        << "</style>\n</head>\n<body>\n<main>\n"
        << "<h1>" << htmlEscape(tr("Ma collection")) << "</h1>\n"
        << "<p class=\"sub\">" << htmlEscape(tr(countText(games.size()))) << " · Nexo Retro · " << date << "</p>\n"
        << "<div class=\"chips\">";
    for (const auto& [name, count] : perSystem)
        out << "<span class=\"chip\">" << htmlEscape(name) << " · " << count << "</span>";
    out << "</div>\n<input id=\"q\" type=\"search\" placeholder=\"" << htmlEscape(tr("Rechercher un jeu")) << "\">\n"
        << "<div class=\"wrap\"><table>\n<thead><tr>";
    for (const std::string& h : headers())
        out << "<th>" << htmlEscape(h) << "</th>";
    out << "</tr></thead>\n<tbody>\n";
    for (const Row& r : rowsOf(games, userData, scraper)) {
        const std::vector<std::string> values = fields(r);
        out << "<tr>";
        for (size_t i = 0; i < values.size(); ++i)
            out << (i + 1 == values.size() ? "<td class=\"file\">" : "<td>") << htmlEscape(values[i]) << "</td>";
        out << "</tr>\n";
    }
    out << "</tbody>\n</table></div>\n</main>\n<script>\n"
        << "document.getElementById('q').addEventListener('input',function(e){var q=e.target.value.toLowerCase();"
           "document.querySelectorAll('tbody tr').forEach(function(r){"
           "r.style.display=r.textContent.toLowerCase().indexOf(q)<0?'none':''})});\n"
        << "</script>\n</body>\n</html>\n";
    return exportedText(games.size(), file);
}

std::vector<Duplicates> findDuplicates(const std::vector<Game>& games)
{
    // Même console + même nom (sans majuscules) : le même jeu.
    std::map<std::string, std::vector<const Game*>> groups;
    for (const Game& g : games) {
        std::string key = std::string(g.system->id) + '|';
        for (const char c : g.title)
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        groups[key].push_back(&g);
    }
    // Identiques ? Même taille, et même début et même fin de fichier (lire
    // un ISO de 4 Go en entier prendrait trop longtemps ; deux versions
    // différentes diffèrent presque toujours dès le premier Mo).
    auto fingerprint = [](const fs::path& file) {
        constexpr std::streamoff kChunk = 1024 * 1024;
        std::ifstream in(file, std::ios::binary);
        Hasher64 hasher;
        std::vector<char> buffer(static_cast<size_t>(kChunk));
        in.read(buffer.data(), kChunk);
        hasher.update(buffer.data(), static_cast<size_t>(in.gcount()));
        in.clear();
        in.seekg(-kChunk, std::ios::end);
        if (in) {
            in.read(buffer.data(), kChunk);
            hasher.update(buffer.data(), static_cast<size_t>(in.gcount()));
        }
        return hasher.finish();
    };
    std::vector<Duplicates> result;
    for (auto& [key, list] : groups) {
        if (list.size() < 2)
            continue;
        Duplicates d;
        d.games = list;
        d.identical = std::all_of(list.begin(), list.end(), [&](const Game* g) { return g->size == list.front()->size; });
        if (d.identical) {
            const uint64_t first = fingerprint(list.front()->path);
            d.identical = std::all_of(list.begin() + 1, list.end(), [&](const Game* g) { return fingerprint(g->path) == first; });
        }
        result.push_back(std::move(d));
    }
    // Les vrais doublons d'abord : ce sont eux qui prennent de la place pour rien.
    std::stable_sort(result.begin(), result.end(),
                     [](const Duplicates& a, const Duplicates& b) { return a.identical && !b.identical; });
    return result;
}

} // namespace library_tools
