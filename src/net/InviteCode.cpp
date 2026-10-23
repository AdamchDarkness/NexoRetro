#include "InviteCode.hpp"

#include <cctype>

namespace net {

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr uint8_t kVersion = 1;

void put16(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    put16(out, v >> 16);
    put16(out, v & 0xFFFF);
}

uint8_t checksum(const std::vector<uint8_t>& bytes, size_t count)
{
    uint8_t sum = 0x5A;
    for (size_t i = 0; i < count; ++i)
        sum = static_cast<uint8_t>((sum << 1 | sum >> 7) ^ bytes[i]); // rotation + ou exclusif
    return sum;
}

} // namespace

// Format binaire :
//   version (1) | salon (4) | nombre d'adresses (1) | adresses (ip 4 + port 2) | contrôle (1)
std::string encodeInvite(const Invite& invite)
{
    std::vector<uint8_t> bytes;
    bytes.push_back(kVersion);
    put32(bytes, invite.session);
    const size_t count = std::min<size_t>(invite.candidates.size(), 5);
    bytes.push_back(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) {
        put32(bytes, invite.candidates[i].ip);
        put16(bytes, invite.candidates[i].port);
    }
    bytes.push_back(checksum(bytes, bytes.size()));

    // Base 32 : on lit les octets 5 bits par 5 bits.
    std::string code;
    uint32_t buffer = 0;
    int bits = 0;
    for (uint8_t b : bytes) {
        buffer = (buffer << 8) | b;
        bits += 8;
        while (bits >= 5) {
            code += kAlphabet[(buffer >> (bits - 5)) & 31];
            bits -= 5;
        }
    }
    if (bits > 0)
        code += kAlphabet[(buffer << (5 - bits)) & 31];

    // Groupes de 4 pour la lisibilité.
    std::string pretty = "EMU";
    for (size_t i = 0; i < code.size(); i += 4)
        pretty += "-" + code.substr(i, 4);
    return pretty;
}

std::optional<Invite> decodeInvite(const std::string& text)
{
    // On garde uniquement les symboles utiles (espaces, tirets et
    // minuscules sont tolérés), et on corrige les confusions courantes.
    std::string code;
    std::string upper;
    for (char c : text)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper.rfind("EMU", 0) == 0)
        upper.erase(0, 3);
    for (char c : upper) {
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (std::isalnum(static_cast<unsigned char>(c)))
            code += c;
    }

    std::vector<uint8_t> bytes;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : code) {
        const char* p = std::char_traits<char>::find(kAlphabet, 32, c);
        if (!p)
            return std::nullopt;
        buffer = (buffer << 5) | static_cast<uint32_t>(p - kAlphabet);
        bits += 5;
        if (bits >= 8) {
            bytes.push_back(static_cast<uint8_t>(buffer >> (bits - 8)));
            bits -= 8;
        }
    }

    if (bytes.size() < 7 || bytes[0] != kVersion)
        return std::nullopt;
    const size_t count = bytes[5];
    const size_t expected = 6 + count * 6 + 1;
    if (count > 5 || bytes.size() < expected || checksum(bytes, expected - 1) != bytes[expected - 1])
        return std::nullopt;

    auto get16 = [&](size_t at) { return static_cast<uint32_t>((bytes[at] << 8) | bytes[at + 1]); };
    Invite invite;
    invite.session = (get16(1) << 16) | get16(3);
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 6 + i * 6;
        invite.candidates.push_back({(get16(at) << 16) | get16(at + 2), static_cast<uint16_t>(get16(at + 4))});
    }
    return invite;
}

} // namespace net
