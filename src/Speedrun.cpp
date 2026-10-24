#include "Speedrun.hpp"

#include "PortablePath.hpp"
#include "UserData.hpp"
#include "Utf8.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace speedrun {

// Format de speedrun.txt (champs séparés par des tabulations) :
//   game  <chemin du jeu>  <catégorie choisie>
//   cat   <nom>  <essais>  <terminées>
//   seg   <nom>  <temps de passage du PB>  <meilleur segment>
//   run   <date>  <temps final>  <splits faits>  <assistée 0/1>  <vidéo>  <passages séparés par des virgules>
// Chaque ligne se rattache à la dernière ligne "game" (ou "cat") au-dessus.

long long Category::sumOfBest() const
{
    long long sum = 0;
    for (const Segment& s : segments) {
        if (s.bestSegment < 0)
            return -1;
        sum += s.bestSegment;
    }
    return segments.empty() ? -1 : sum;
}

Category& GameRuns::category()
{
    if (categories.empty())
        categories.push_back({});
    current = std::clamp(current, 0, static_cast<int>(categories.size()) - 1);
    Category& c = categories[static_cast<size_t>(current)];
    if (c.segments.empty())
        c.segments.push_back({"Fin"});
    return c;
}

std::string formatTime(long long ms, bool showSign, int decimals)
{
    if (ms == -1 && !showSign)
        return "—";
    const bool negative = ms < 0;
    long long v = negative ? -ms : ms;
    const long long hours = v / 3600000;
    const long long minutes = v / 60000 % 60;
    const long long seconds = v / 1000 % 60;
    const long long fraction = decimals == 1 ? v / 100 % 10 : v / 10 % 100;
    char text[48];
    const char* sign = negative ? "-" : (showSign ? "+" : "");
    if (hours > 0)
        std::snprintf(text, sizeof(text), "%s%lld:%02lld:%02lld", sign, hours, minutes, seconds);
    else if (minutes > 0)
        std::snprintf(text, sizeof(text), "%s%lld:%02lld", sign, minutes, seconds);
    else
        std::snprintf(text, sizeof(text), "%s%lld", sign, seconds);
    std::string out = text;
    // Les dixièmes / centièmes : seulement sous l'heure (au-delà, ils ne
    // se lisent plus).
    if (decimals > 0 && hours == 0) {
        char frac[8];
        std::snprintf(frac, sizeof(frac), decimals == 1 ? ".%lld" : ".%02lld", fraction);
        out += frac;
    }
    return out;
}

Speedrun::Speedrun(fs::path file) : m_file(std::move(file)), m_base(portable::baseOf(m_file.parent_path())) {}

void Speedrun::load()
{
    m_games.clear();
    std::ifstream in(m_file);
    std::string line;
    GameRuns* game = nullptr;
    Category* cat = nullptr;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::istringstream s(line);
        std::string field;
        while (std::getline(s, field, '\t'))
            f.push_back(field);
        if (f.empty())
            continue;
        if (f[0] == "game" && f.size() >= 3) {
            game = &m_games[portable::load(f[1], m_base)];
            game->current = std::atoi(f[2].c_str());
            cat = nullptr;
        } else if (f[0] == "cat" && f.size() >= 4 && game) {
            game->categories.push_back({});
            cat = &game->categories.back();
            cat->name = f[1];
            cat->attempts = std::atoi(f[2].c_str());
            cat->finished = std::atoi(f[3].c_str());
        } else if (f[0] == "seg" && f.size() >= 4 && cat) {
            cat->segments.push_back({f[1], std::atoll(f[2].c_str()), std::atoll(f[3].c_str())});
        } else if (f[0] == "run" && f.size() >= 6 && cat) {
            Run r;
            r.date = std::atoll(f[1].c_str());
            r.timeMs = std::atoll(f[2].c_str());
            r.splitsDone = std::atoi(f[3].c_str());
            r.assisted = f[4] == "1";
            r.video = portable::load(f[5], m_base);
            if (f.size() >= 7) {
                std::istringstream list(f[6]);
                std::string value;
                while (std::getline(list, value, ','))
                    r.splits.push_back(std::atoll(value.c_str()));
            }
            cat->history.push_back(std::move(r));
        }
    }
}

void Speedrun::save() const
{
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    const fs::path temp = m_file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        for (const auto& [key, game] : m_games) {
            out << "game\t" << portable::store(key, m_base) << '\t' << game.current << '\n';
            for (const Category& c : game.categories) {
                out << "cat\t" << c.name << '\t' << c.attempts << '\t' << c.finished << '\n';
                for (const Segment& s : c.segments)
                    out << "seg\t" << s.name << '\t' << s.pbSplit << '\t' << s.bestSegment << '\n';
                for (const Run& r : c.history) {
                    out << "run\t" << r.date << '\t' << r.timeMs << '\t' << r.splitsDone << '\t' << (r.assisted ? 1 : 0)
                        << '\t' << portable::store(r.video, m_base) << '\t';
                    for (size_t i = 0; i < r.splits.size(); ++i)
                        out << (i ? "," : "") << r.splits[i];
                    out << '\n';
                }
            }
        }
    }
    // (Écrit à côté puis renommé : un plantage en pleine écriture ne perd pas les records.)
    fs::rename(temp, m_file, ec);
}

GameRuns& Speedrun::game(const fs::path& gamePath)
{
    GameRuns& g = m_games[UserData::keyOf(gamePath)];
    g.category(); // (au moins une catégorie, avec un split)
    return g;
}

const GameRuns* Speedrun::find(const fs::path& gamePath) const
{
    const auto it = m_games.find(UserData::keyOf(gamePath));
    return it == m_games.end() ? nullptr : &it->second;
}

void Speedrun::forgetGame(const fs::path& gamePath)
{
    if (UserData::keyOf(gamePath) == m_gameKey)
        reset();
    m_games.erase(UserData::keyOf(gamePath));
    save();
}

void Speedrun::setGame(const fs::path& gamePath, std::string title)
{
    if (m_state == State::Running)
        reset(); // (on quitte le jeu en pleine run : elle compte comme abandonnée)
    m_state = State::Ready;
    m_splits.clear();
    m_split = 0;
    m_gamePath = gamePath;
    m_gameKey = gamePath.empty() ? std::string() : UserData::keyOf(gamePath);
    m_title = std::move(title);
    if (gamePath.empty())
        m_enabled = false;
}

Category* Speedrun::category()
{
    if (m_gameKey.empty())
        return nullptr;
    return &game(m_gamePath).category();
}

long long Speedrun::elapsedMs() const
{
    if (m_state == State::Ready)
        return 0;
    const Uint64 end = m_state == State::Finished ? m_endNs : SDL_GetTicksNS();
    return static_cast<long long>((end - m_startNs) / 1000000);
}

void Speedrun::startOrSplit()
{
    Category* cat = category();
    if (!cat)
        return;
    m_enabled = true;
    if (m_state == State::Finished)
        return; // (il faut d'abord remettre à zéro)
    if (m_state == State::Ready) {
        m_state = State::Running;
        m_startNs = SDL_GetTicksNS();
        m_startDate = static_cast<long long>(std::time(nullptr));
        m_split = 0;
        m_splits.assign(cat->segments.size(), -1);
        m_golds.assign(cat->segments.size(), 0);
        m_compare.clear();
        for (const Segment& s : cat->segments)
            m_compare.push_back(s.pbSplit);
        m_assisted = false;
        m_lastWasPb = false;
        ++cat->attempts;
        m_started = true;
        save();
        return;
    }
    // Passage du split en cours.
    const long long now = elapsedMs();
    m_splits[static_cast<size_t>(m_split)] = now;
    // Meilleur segment ? (depuis le dernier split qui a un temps)
    long long previous = 0;
    for (int i = m_split - 1; i >= 0; --i)
        if (m_splits[static_cast<size_t>(i)] >= 0) {
            previous = m_splits[static_cast<size_t>(i)];
            break;
        }
    const bool contiguous = m_split == 0 || m_splits[static_cast<size_t>(m_split - 1)] >= 0;
    Segment& seg = cat->segments[static_cast<size_t>(m_split)];
    if (!m_assisted && contiguous && (seg.bestSegment < 0 || now - previous < seg.bestSegment)) {
        // (Le tout premier temps d'un segment n'est pas un "gold" : il n'a rien battu.)
        m_golds[static_cast<size_t>(m_split)] = seg.bestSegment >= 0;
        seg.bestSegment = now - previous;
    }
    ++m_split;
    if (m_split >= static_cast<int>(cat->segments.size()))
        finishRun(true);
}

void Speedrun::undoSplit()
{
    // (Une run terminée est déjà enregistrée, record compris : on n'y revient pas.)
    if (m_state != State::Running || m_split == 0)
        return;
    --m_split;
    m_splits[static_cast<size_t>(m_split)] = -1;
    m_golds[static_cast<size_t>(m_split)] = 0;
}

void Speedrun::skipSplit()
{
    Category* cat = category();
    // (Le dernier split ne se saute pas : c'est lui qui arrête le chrono.)
    if (m_state != State::Running || !cat || m_split + 1 >= static_cast<int>(cat->segments.size()))
        return;
    m_splits[static_cast<size_t>(m_split)] = -1;
    ++m_split;
}

void Speedrun::reset()
{
    if (m_state == State::Running)
        finishRun(false);
    m_state = State::Ready;
    m_split = 0;
    m_splits.clear();
}

void Speedrun::markAssisted()
{
    if (m_state == State::Running)
        m_assisted = true;
}

void Speedrun::finishRun(bool completed)
{
    Category* cat = category();
    if (!cat)
        return;
    Run run;
    run.date = m_startDate;
    run.assisted = m_assisted;
    run.splits = m_splits;
    run.splitsDone = m_split;
    if (completed) {
        m_endNs = SDL_GetTicksNS();
        m_state = State::Finished;
        run.timeMs = m_splits.back();
        ++cat->finished;
        // Nouveau record : ses temps de passage deviennent la référence.
        m_lastWasPb = !m_assisted && (cat->pb() < 0 || run.timeMs < cat->pb());
        if (m_lastWasPb)
            for (size_t i = 0; i < cat->segments.size(); ++i)
                cat->segments[i].pbSplit = m_splits[i];
    }
    cat->history.push_back(std::move(run));
    m_ended = true;
    save();
}

long long Speedrun::comparison(int split) const
{
    if (m_state != State::Ready && split >= 0 && split < static_cast<int>(m_compare.size()))
        return m_compare[static_cast<size_t>(split)];
    // (Avant le départ : le record actuel.)
    const auto it = m_games.find(m_gameKey);
    if (it == m_games.end() || it->second.categories.empty())
        return -1;
    const GameRuns& g = it->second;
    const Category& c = g.categories[static_cast<size_t>(std::clamp(g.current, 0, static_cast<int>(g.categories.size()) - 1))];
    return split >= 0 && split < static_cast<int>(c.segments.size()) ? c.segments[static_cast<size_t>(split)].pbSplit : -1;
}

bool Speedrun::takeStarted()
{
    const bool v = m_started;
    m_started = false;
    return v;
}

bool Speedrun::takeEnded()
{
    const bool v = m_ended;
    m_ended = false;
    return v;
}

const Run* Speedrun::lastRun()
{
    Category* cat = category();
    return cat && !cat->history.empty() ? &cat->history.back() : nullptr;
}

void Speedrun::attachVideo(const std::string& file)
{
    Category* cat = category();
    if (!cat || cat->history.empty())
        return;
    cat->history.back().video = file;
    save();
}

} // namespace speedrun
