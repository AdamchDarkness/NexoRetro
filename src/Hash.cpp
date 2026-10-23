#include "Hash.hpp"

#include <cstring>

namespace {
constexpr uint64_t kMul1 = 0x87C37B91114253D5ull;
constexpr uint64_t kMul2 = 0x4CF5AD432745937Full;

uint64_t rotl(uint64_t x, int r)
{
    return (x << r) | (x >> (64 - r));
}

// Mélange final : chaque bit de l'entrée influence tous les bits de la sortie.
uint64_t avalanche(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}
} // namespace

void Hasher64::mix(uint64_t word)
{
    word *= kMul1;
    word = rotl(word, 31);
    word *= kMul2;
    m_state ^= word;
    m_state = rotl(m_state, 27) * 5 + 0x52DCE729;
}

void Hasher64::update(const void* data, size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    m_length += size;

    // Compléter un mot commencé au morceau précédent.
    while (m_pendingSize > 0 && size > 0) {
        m_pending[m_pendingSize++] = *bytes++;
        --size;
        if (m_pendingSize == 8) {
            uint64_t w;
            std::memcpy(&w, m_pending, 8);
            mix(w);
            m_pendingSize = 0;
        }
    }
    // Le gros des données, 8 octets à la fois.
    while (size >= 8) {
        uint64_t w;
        std::memcpy(&w, bytes, 8); // memcpy : lecture sûre même mal alignée
        mix(w);
        bytes += 8;
        size -= 8;
    }
    // Le reste attend le morceau suivant (ou finish()).
    std::memcpy(m_pending, bytes, size);
    m_pendingSize = size;
}

uint64_t Hasher64::finish() const
{
    uint64_t h = m_state;
    if (m_pendingSize > 0) {
        uint64_t w = 0;
        std::memcpy(&w, m_pending, m_pendingSize);
        w *= kMul1;
        w = rotl(w, 31);
        w *= kMul2;
        h ^= w;
    }
    h ^= m_length;
    return avalanche(h);
}

uint64_t Hasher64::of(std::string_view text, uint64_t seed)
{
    Hasher64 h(seed);
    h.update(text.data(), text.size());
    return h.finish();
}

namespace {
// La table des 256 restes possibles, calculée une seule fois.
// [C++] Une fonction "constexpr" peut être exécutée par le COMPILATEUR : la
// table est calculée pendant la compilation et rangée dans l'exécutable.
struct Crc32Table {
    uint32_t values[256];
    constexpr Crc32Table() : values()
    {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            values[i] = c;
        }
    }
};
constexpr Crc32Table kCrcTable;
} // namespace

uint32_t crc32(uint32_t crc, const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = kCrcTable.values[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
