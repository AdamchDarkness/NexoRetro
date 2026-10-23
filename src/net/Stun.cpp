#include "Stun.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace net {

namespace {
constexpr uint32_t kMagic = 0x2112A442;
constexpr uint16_t kBindingRequest = 0x0001;
constexpr uint16_t kBindingResponse = 0x0101;
constexpr uint16_t kMappedAddress = 0x0001;
constexpr uint16_t kXorMappedAddress = 0x0020;

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t read32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
} // namespace

StunRequest::StunRequest()
{
    for (uint8_t& b : m_transaction)
        b = static_cast<uint8_t>(SDL_rand(256));
}

std::vector<uint8_t> StunRequest::bytes() const
{
    // En-tête : type (2), longueur des attributs (2), nombre magique (4),
    // identifiant de transaction (12). Pas d'attribut.
    std::vector<uint8_t> p = {kBindingRequest >> 8, kBindingRequest & 0xFF, 0, 0,
                              (kMagic >> 24) & 0xFF, (kMagic >> 16) & 0xFF, (kMagic >> 8) & 0xFF, kMagic & 0xFF};
    p.insert(p.end(), m_transaction.begin(), m_transaction.end());
    return p;
}

bool StunRequest::looksLikeStun(const uint8_t* data, size_t size)
{
    return size >= 20 && read32(data + 4) == kMagic;
}

std::optional<Address> StunRequest::parseResponse(const uint8_t* data, size_t size) const
{
    if (!looksLikeStun(data, size) || read16(data) != kBindingResponse)
        return std::nullopt;
    if (std::memcmp(data + 8, m_transaction.data(), m_transaction.size()) != 0)
        return std::nullopt; // réponse à une autre question
    const size_t length = std::min<size_t>(read16(data + 2), size - 20);

    std::optional<Address> plain;
    // Les attributs se suivent : type (2), longueur (2), valeur (alignée sur 4).
    for (size_t pos = 20; pos + 4 <= 20 + length;) {
        const uint16_t type = read16(data + pos);
        const uint16_t len = read16(data + pos + 2);
        const uint8_t* value = data + pos + 4;
        if (pos + 4 + len > size)
            break;
        if ((type == kXorMappedAddress || type == kMappedAddress) && len >= 8 && value[1] == 0x01 /* IPv4 */) {
            uint16_t port = read16(value + 2);
            uint32_t ip = read32(value + 4);
            if (type == kXorMappedAddress) {
                // L'adresse est "brouillée" avec le nombre magique, pour que
                // certaines box trop zélées ne la réécrivent pas.
                port ^= static_cast<uint16_t>(kMagic >> 16);
                ip ^= kMagic;
                return Address{ip, port};
            }
            plain = Address{ip, port};
        }
        pos += 4 + ((len + 3) & ~size_t(3));
    }
    return plain;
}

} // namespace net
