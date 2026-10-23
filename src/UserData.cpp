#include "UserData.hpp"

#include "Utf8.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

// Format du fichier (champs séparés par des tabulations) :
//   game    <chemin>  <dernière partie>  <secondes>  <favori 0/1>  <jaquette>  <nom choisi>
//   folder  <chemin>
//   file    <chemin>  <console>
//   hidden  <chemin>  (jeu retiré de la bibliothèque)

UserData::UserData(fs::path file) : m_file(std::move(file)) {}

std::string UserData::keyOf(const fs::path& path)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    return toUtf8((ec ? path : absolute).lexically_normal());
}

void UserData::load()
{
    std::ifstream in(m_file);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::istringstream stream(line);
        std::string field;
        while (std::getline(stream, field, '\t'))
            fields.push_back(field);
        if (fields.empty())
            continue;

        if (fields[0] == "game" && fields.size() >= 5) {
            GameRecord& r = m_games[fields[1]];
            r.lastPlayed = std::atoll(fields[2].c_str());
            r.playSeconds = std::atoll(fields[3].c_str());
            r.favorite = fields[4] == "1";
            if (fields.size() >= 6)
                r.cover = fields[5];
            if (fields.size() >= 7)
                r.title = fields[6];
            if (fields.size() >= 8)
                r.controller = fields[7];
        } else if (fields[0] == "folder" && fields.size() >= 2) {
            m_folders.push_back(fromUtf8(fields[1]));
        } else if (fields[0] == "file" && fields.size() >= 3) {
            m_manual[fields[1]] = fields[2];
        } else if (fields[0] == "hidden" && fields.size() >= 2) {
            m_hidden.insert(fields[1]);
        }
    }
}

void UserData::save() const
{
    std::ofstream out(m_file, std::ios::trunc);
    for (const auto& [key, r] : m_games) {
        out << "game\t" << key << '\t' << r.lastPlayed << '\t' << r.playSeconds << '\t' << (r.favorite ? 1 : 0) << '\t'
            << r.cover << '\t' << r.title << '\t' << r.controller << '\n';
    }
    for (const fs::path& folder : m_folders)
        out << "folder\t" << toUtf8(folder) << '\n';
    for (const auto& [key, system] : m_manual)
        out << "file\t" << key << '\t' << system << '\n';
    for (const std::string& key : m_hidden)
        out << "hidden\t" << key << '\n';
}

GameRecord& UserData::record(const fs::path& game)
{
    return m_games[keyOf(game)];
}

const GameRecord* UserData::find(const fs::path& game) const
{
    const auto it = m_games.find(keyOf(game));
    return it == m_games.end() ? nullptr : &it->second;
}

void UserData::addWatchedFolder(const fs::path& folder)
{
    const std::string key = keyOf(folder);
    for (const fs::path& f : m_folders) {
        if (keyOf(f) == key)
            return;
    }
    m_folders.push_back(fromUtf8(key));
}

void UserData::removeWatchedFolder(const fs::path& folder)
{
    const std::string key = keyOf(folder);
    std::erase_if(m_folders, [&](const fs::path& f) { return keyOf(f) == key; });
}

void UserData::addManualGame(const fs::path& file, const std::string& systemId)
{
    m_manual[keyOf(file)] = systemId;
    m_hidden.erase(keyOf(file)); // ajouté à nouveau : il réapparaît
}

void UserData::hide(const fs::path& game)
{
    const std::string key = keyOf(game);
    // Un jeu ajouté à la main : il suffit de l'oublier.
    if (m_manual.erase(key) == 0)
        m_hidden.insert(key);
}

bool UserData::isHidden(const fs::path& game) const
{
    return m_hidden.count(keyOf(game)) > 0;
}

void UserData::forget(const fs::path& game)
{
    const std::string key = keyOf(game);
    m_games.erase(key);
    m_manual.erase(key);
    m_hidden.erase(key);
}
