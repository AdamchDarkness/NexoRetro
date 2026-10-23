#include "Remaps.hpp"

#include <fstream>
#include <sstream>

Remaps::Remaps(std::filesystem::path file) : m_file(std::move(file))
{
    load();
}

Remaps::Table Remaps::identity()
{
    Table t{};
    for (int i = 0; i < kButtons; ++i)
        t[i] = i;
    t[kLeftStick] = 0;     // stick gauche -> stick gauche
    t[kLeftStick + 1] = 1; // stick droit -> stick droit
    return t;
}

bool Remaps::valid(int slot, int source)
{
    if (slot >= 0 && slot < kButtons)
        return source >= kNone && source < kButtons;
    if (slot >= kLeftStick && slot < kSlots)
        return source >= kNone && source < 4; // 2 sticks, inversés ou non
    return false;
}

Remaps::Table Remaps::get(const std::string& key) const
{
    const auto it = m_tables.find(key);
    return it != m_tables.end() ? it->second : identity();
}

void Remaps::set(const std::string& key, int coreButton, int sourceButton)
{
    if (!valid(coreButton, sourceButton))
        return;
    // [C++] try_emplace : crée la table (à l'identité) seulement si elle manque.
    auto [it, created] = m_tables.try_emplace(key, identity());
    it->second[coreButton] = sourceButton;
    if (it->second == identity())
        m_tables.erase(it); // revenue aux réglages d'origine : inutile de la garder
    save();
}

void Remaps::reset(const std::string& key)
{
    m_tables.erase(key);
    save();
}

void Remaps::load()
{
    std::ifstream in(m_file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t a = line.find('\t');
        const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos)
            continue;
        const int core = std::atoi(line.c_str() + a + 1);
        const int source = std::atoi(line.c_str() + b + 1);
        if (!valid(core, source))
            continue;
        auto [it, created] = m_tables.try_emplace(line.substr(0, a), identity());
        it->second[core] = source;
    }
}

void Remaps::save() const
{
    std::ofstream out(m_file);
    for (const auto& [key, table] : m_tables)
        for (int i = 0; i < kSlots; ++i)
            if (table[i] != identity()[i]) // seulement ce qui diffère des réglages d'origine
                out << key << '\t' << i << '\t' << table[i] << '\n';
}
