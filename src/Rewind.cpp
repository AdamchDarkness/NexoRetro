#include "Rewind.hpp"

#include <cstdint>
#include <cstring>

namespace {

// Compression des différences : une suite de blocs
//   [nombre de zéros : 4 octets][nombre d'octets non nuls : 4 octets][ces octets]
// Une différence de 400 Ko où seuls quelques centaines d'octets ont changé
// tient en quelques Ko.
void put32(std::string& out, uint32_t v)
{
    char b[4];
    std::memcpy(b, &v, 4);
    out.append(b, 4);
}

uint32_t get32(const char* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// (a XOR b), compressé.
std::string encodeXor(const std::string& a, const std::string& b)
{
    std::string out;
    const size_t n = a.size();
    size_t i = 0;
    while (i < n) {
        size_t zeros = 0;
        while (i + zeros < n && a[i + zeros] == b[i + zeros])
            ++zeros;
        size_t start = i + zeros;
        size_t literal = 0;
        // Une suite d'octets différents ; on s'arrête à 8 zéros d'affilée
        // (moins, ce ne serait pas rentable de couper le bloc).
        while (start + literal < n) {
            size_t run = 0;
            while (start + literal + run < n && run < 8 && a[start + literal + run] == b[start + literal + run])
                ++run;
            if (run >= 8 || start + literal + run == n)
                break;
            literal += run + 1;
        }
        put32(out, static_cast<uint32_t>(zeros));
        put32(out, static_cast<uint32_t>(literal));
        for (size_t k = 0; k < literal; ++k)
            out.push_back(static_cast<char>(a[start + k] ^ b[start + k]));
        i = start + literal;
    }
    return out;
}

// state ^= différence (décompressée au vol).
void applyXor(std::string& state, const std::string& delta)
{
    size_t pos = 0;
    const char* p = delta.data();
    const char* end = p + delta.size();
    while (p + 8 <= end) {
        pos += get32(p);
        const uint32_t literal = get32(p + 4);
        p += 8;
        for (uint32_t k = 0; k < literal && pos < state.size(); ++k)
            state[pos++] ^= *p++;
    }
}

} // namespace

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
    out = m_current;
    return true;
}
