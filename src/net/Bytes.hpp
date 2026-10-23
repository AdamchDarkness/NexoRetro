#pragma once
// Bytes : écrire et relire des messages réseau octet par octet.
//
// Un message est une suite d'octets. Pour y ranger des nombres et des
// textes, on choisit un ordre fixe (gros-boutiste, le plus fort d'abord),
// pour que tous les PC le relisent de la même façon.

#include <cstdint>
#include <string>
#include <string_view>

namespace net {

class ByteWriter {
public:
    ByteWriter& u8(uint8_t v)
    {
        m_data += static_cast<char>(v);
        return *this;
    }
    ByteWriter& u16(uint16_t v) { return u8(static_cast<uint8_t>(v >> 8)).u8(static_cast<uint8_t>(v)); }
    ByteWriter& u32(uint32_t v) { return u16(static_cast<uint16_t>(v >> 16)).u16(static_cast<uint16_t>(v)); }
    ByteWriter& u64(uint64_t v) { return u32(static_cast<uint32_t>(v >> 32)).u32(static_cast<uint32_t>(v)); }
    // Texte : sa longueur (1 octet, 255 au plus) puis ses octets.
    ByteWriter& str(std::string_view s)
    {
        const size_t n = s.size() < 255 ? s.size() : 255;
        u8(static_cast<uint8_t>(n));
        m_data.append(s.data(), n);
        return *this;
    }
    // Le reste du message, sans longueur.
    ByteWriter& raw(std::string_view s)
    {
        m_data.append(s.data(), s.size());
        return *this;
    }
    const std::string& data() const { return m_data; }

private:
    std::string m_data;
};

// Relecture. Si le message est trop court (paquet abîmé ou malveillant), les
// lectures renvoient 0 / "" et ok() devient faux : on ne lit jamais hors du message.
class ByteReader {
public:
    explicit ByteReader(std::string_view data) : m_data(data) {}

    uint8_t u8()
    {
        if (m_pos + 1 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        return static_cast<uint8_t>(m_data[m_pos++]);
    }
    uint16_t u16() { const uint16_t hi = u8(); return static_cast<uint16_t>((hi << 8) | u8()); }
    uint32_t u32() { const uint32_t hi = u16(); return (hi << 16) | u16(); }
    uint64_t u64() { const uint64_t hi = u32(); return (hi << 32) | u32(); }
    std::string str()
    {
        const size_t n = u8();
        if (m_pos + n > m_data.size()) {
            m_ok = false;
            return {};
        }
        std::string s(m_data.substr(m_pos, n));
        m_pos += n;
        return s;
    }
    std::string rest()
    {
        std::string s(m_data.substr(m_pos));
        m_pos = m_data.size();
        return s;
    }
    bool ok() const { return m_ok; }

private:
    std::string_view m_data;
    size_t m_pos = 0;
    bool m_ok = true;
};

} // namespace net
