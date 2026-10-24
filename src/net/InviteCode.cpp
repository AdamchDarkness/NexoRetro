#include "InviteCode.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace net {

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr uint8_t kVersion = 1;
constexpr uint8_t kRelayInviteVersion = 2; // salon par relais
constexpr uint8_t kRelayCodeVersion = 3;   // code d'un relais
constexpr size_t kShortSymbols = 16; // 10 octets = 80 bits = 16 symboles de 5 bits

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

// Base 32 : on lit les octets 5 bits par 5 bits, et on les présente par
// groupes de 4 pour la lisibilité ("EMU-7K3P-Q9XW-...").
std::string toCode(const std::vector<uint8_t>& bytes, const char* prefix = "EMU")
{
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

    std::string pretty = prefix;
    for (size_t i = 0; i < code.size(); i += 4)
        pretty += "-" + code.substr(i, 4);
    return pretty;
}

} // namespace

// Format binaire du code complet :
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
    return toCode(bytes);
}

// Format binaire du code court (toujours 10 octets, donc 16 symboles) :
//   salon (3) | ip (4) | port (2) | contrôle (1)
std::string encodeShortInvite(uint32_t session, const Address& address)
{
    std::vector<uint8_t> bytes;
    bytes.push_back(static_cast<uint8_t>(session >> 16));
    put16(bytes, session & 0xFFFF);
    put32(bytes, address.ip);
    put16(bytes, address.port);
    bytes.push_back(static_cast<uint8_t>(checksum(bytes, bytes.size()) ^ 0xA5)); // ^ 0xA5 : distinct du code complet
    return toCode(bytes);
}

std::optional<Invite> decodeInvite(const std::string& text)
{
    // On garde uniquement les symboles utiles (espaces, tirets et
    // minuscules sont tolérés), et on corrige les confusions courantes.
    std::string code;
    std::string upper;
    for (char c : text)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper.rfind("EMU", 0) == 0 || upper.rfind("RLY", 0) == 0)
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
    auto get16 = [&](size_t at) { return static_cast<uint32_t>((bytes[at] << 8) | bytes[at + 1]); };

    // Code court : une seule adresse (les PC du même réseau local se
    // trouvent tout seuls, voir Session::join).
    if (code.size() == kShortSymbols) {
        if (bytes.size() != 10 || static_cast<uint8_t>(checksum(bytes, 9) ^ 0xA5) != bytes[9])
            return std::nullopt;
        Invite invite;
        invite.session = (static_cast<uint32_t>(bytes[0]) << 16) | get16(1);
        invite.candidates.push_back({(get16(3) << 16) | get16(5), static_cast<uint16_t>(get16(7))});
        return invite;
    }

    // Salon par relais :
    //   version (1) | salon (4) | secret (16) | nombre d'adresses (1) | adresses | contrôle (1)
    if (bytes.size() >= 23 && bytes[0] == kRelayInviteVersion) {
        const size_t count = bytes[21];
        const size_t expected = 22 + count * 6 + 1;
        if (count == 0 || count > 5 || bytes.size() < expected || checksum(bytes, expected - 1) != bytes[expected - 1])
            return std::nullopt;
        Invite invite;
        invite.relay = true;
        invite.session = (get16(1) << 16) | get16(3);
        std::copy(bytes.begin() + 5, bytes.begin() + 21, invite.secret.begin());
        for (size_t i = 0; i < count; ++i) {
            const size_t at = 22 + i * 6;
            invite.candidates.push_back({(get16(at) << 16) | get16(at + 2), static_cast<uint16_t>(get16(at + 4))});
        }
        return invite;
    }
    if (bytes.size() < 7 || bytes[0] != kVersion)
        return std::nullopt;
    const size_t count = bytes[5];
    const size_t expected = 6 + count * 6 + 1;
    if (count > 5 || bytes.size() < expected || checksum(bytes, expected - 1) != bytes[expected - 1])
        return std::nullopt;

    Invite invite;
    invite.session = (get16(1) << 16) | get16(3);
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 6 + i * 6;
        invite.candidates.push_back({(get16(at) << 16) | get16(at + 2), static_cast<uint16_t>(get16(at + 4))});
    }
    return invite;
}

std::string encodeRelayInvite(uint32_t room, const std::array<uint8_t, 16>& secret, const std::vector<Address>& relay)
{
    std::vector<uint8_t> bytes;
    bytes.push_back(kRelayInviteVersion);
    put32(bytes, room);
    bytes.insert(bytes.end(), secret.begin(), secret.end());
    const size_t count = std::min<size_t>(relay.size(), 5);
    bytes.push_back(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) {
        put32(bytes, relay[i].ip);
        put16(bytes, relay[i].port);
    }
    bytes.push_back(checksum(bytes, bytes.size()));
    return toCode(bytes);
}

// Code d'un relais : version (1) | nombre d'adresses (1) | adresses | contrôle (1)
std::string encodeRelayCode(const std::vector<Address>& relay)
{
    std::vector<uint8_t> bytes;
    bytes.push_back(kRelayCodeVersion);
    const size_t count = std::min<size_t>(relay.size(), 5);
    bytes.push_back(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) {
        put32(bytes, relay[i].ip);
        put16(bytes, relay[i].port);
    }
    bytes.push_back(checksum(bytes, bytes.size()));
    return toCode(bytes, "RLY");
}

std::optional<std::vector<Address>> decodeRelayCode(const std::string& code)
{
    // (Même lecture que les invitations : on décode les symboles, puis on regarde la version.)
    std::string upper;
    for (char ch : code)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (upper.rfind("RLY", 0) == 0 || upper.rfind("EMU", 0) == 0)
        upper.erase(0, 3);
    std::vector<uint8_t> bytes;
    uint32_t buffer = 0;
    int bits = 0;
    for (char ch : upper) {
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        if (!std::isalnum(static_cast<unsigned char>(ch)))
            continue;
        const char* p = std::char_traits<char>::find(kAlphabet, 32, ch);
        if (!p)
            return std::nullopt;
        buffer = (buffer << 5) | static_cast<uint32_t>(p - kAlphabet);
        bits += 5;
        if (bits >= 8) {
            bytes.push_back(static_cast<uint8_t>(buffer >> (bits - 8)));
            bits -= 8;
        }
    }
    if (bytes.size() < 3 || bytes[0] != kRelayCodeVersion)
        return std::nullopt;
    const size_t count = bytes[1];
    const size_t expected = 2 + count * 6 + 1;
    if (count == 0 || count > 5 || bytes.size() < expected || checksum(bytes, expected - 1) != bytes[expected - 1])
        return std::nullopt;
    std::vector<Address> list;
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 2 + i * 6;
        list.push_back({(static_cast<uint32_t>(bytes[at]) << 24) | (static_cast<uint32_t>(bytes[at + 1]) << 16) |
                            (static_cast<uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3],
                        static_cast<uint16_t>((bytes[at + 4] << 8) | bytes[at + 5])});
    }
    return list;
}

} // namespace net
