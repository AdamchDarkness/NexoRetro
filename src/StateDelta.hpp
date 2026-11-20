#pragma once
// StateDelta : la différence entre deux "photos" de l'émulateur (sauvegardes
// d'état), compressée. D'une image à la suivante presque rien ne change :
//   différence = photo(a) XOR photo(b)
// est faite presque entièrement de zéros, qu'on compte au lieu de les
// garder. Et photo(b) = photo(a) XOR différence (le XOR s'annule quand on
// l'applique deux fois).
//
// Sert au retour en arrière (Rewind) et au rollback du jeu en ligne pour
// les consoles aux états très lourds (Netplay : PS2, GameCube...).

#include <cstdint>
#include <cstring>
#include <string>

namespace statedelta {

// Compression des différences : une suite de blocs
//   [nombre de zéros : 4 octets][nombre d'octets non nuls : 4 octets][ces octets]
// Une différence de 400 Ko où seuls quelques centaines d'octets ont changé
// tient en quelques Ko.
inline void put32(std::string& out, uint32_t v)
{
    char b[4];
    std::memcpy(b, &v, 4);
    out.append(b, 4);
}

inline uint32_t get32(const char* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// (a XOR b), compressé.
inline std::string encodeXor(const std::string& a, const std::string& b)
{
    std::string out;
    const size_t n = a.size();
    // (Pointeurs bruts : cette boucle tourne sur plusieurs Mo, 60 fois par
    // seconde pour le retour en arrière.)
    const char* pa = a.data();
    const char* pb = b.data();
    size_t i = 0;
    while (i < n) {
        // Les octets identiques, comparés 8 à la fois tant que c'est possible.
        size_t zeros = 0;
        while (i + zeros + 8 <= n && std::memcmp(pa + i + zeros, pb + i + zeros, 8) == 0)
            zeros += 8;
        while (i + zeros < n && pa[i + zeros] == pb[i + zeros])
            ++zeros;
        size_t start = i + zeros;
        size_t literal = 0;
        // Une suite d'octets différents ; on s'arrête à 8 zéros d'affilée
        // (moins, ce ne serait pas rentable de couper le bloc).
        while (start + literal < n) {
            size_t run = 0;
            while (start + literal + run < n && run < 8 && pa[start + literal + run] == pb[start + literal + run])
                ++run;
            if (run >= 8 || start + literal + run == n)
                break;
            literal += run + 1;
        }
        put32(out, static_cast<uint32_t>(zeros));
        put32(out, static_cast<uint32_t>(literal));
        // (D'un bloc : bien plus rapide qu'un octet à la fois.)
        const size_t at = out.size();
        out.resize(at + literal);
        char* dst = out.data() + at;
        for (size_t k = 0; k < literal; ++k)
            dst[k] = static_cast<char>(pa[start + k] ^ pb[start + k]);
        i = start + literal;
    }
    return out;
}

// state ^= différence (décompressée au vol).
inline void applyXor(std::string& state, const std::string& delta)
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


} // namespace statedelta
