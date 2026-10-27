#include "Http.hpp"

// urlEncode / urlDecode : du texte pur, les mêmes sous Windows et Android
// (le téléchargement lui-même, lui, dépend du système : Http.cpp, android/Http.cpp).

namespace net {

std::string urlEncode(std::string_view text)
{
    static const char* const hex = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Les caractères "non réservés" passent tels quels ; tout le reste
        // (espaces, parenthèses, accents en UTF-8...) devient %XX.
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '_' ||
            u == '.' || u == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[u >> 4];
            out += hex[u & 15];
        }
    }
    return out;
}

std::string urlDecode(std::string_view text)
{
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = value(text[i + 1]);
            const int lo = value(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

} // namespace net
