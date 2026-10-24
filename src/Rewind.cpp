#include "Rewind.hpp"

#include "StateDelta.hpp"

// (La compression des différences est dans StateDelta.hpp.)
using statedelta::applyXor;
using statedelta::encodeXor;

void Rewind::reset()
{
    m_current.clear();
    m_deltas.clear();
    m_bytes = 0;
}

void Rewind::push(const std::string& state)
{
    // Taille différente (rare : changement de mode du core) : on repart de zéro.
    if (m_current.size() != state.size()) {
        reset();
        m_current = state;
        return;
    }
    // La différence nous ramène de "state" à la photo d'avant (m_current).
    m_deltas.push_back(encodeXor(state, m_current));
    m_bytes += m_deltas.back().size();
    ++m_newest;
    m_current = state;
    // Mémoire pleine : on oublie le passé le plus lointain.
    while (m_bytes > m_budget && !m_deltas.empty()) {
        m_bytes -= m_deltas.front().size();
        m_deltas.pop_front();
    }
}

bool Rewind::pop(std::string& out)
{
    if (m_deltas.empty())
        return false;
    applyXor(m_current, m_deltas.back());
    m_bytes -= m_deltas.back().size();
    m_deltas.pop_back();
    --m_newest;
    out = m_current;
    return true;
}
