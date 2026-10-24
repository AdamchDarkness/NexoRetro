#pragma once
// Json : lire un texte JSON (réponses des API web : GitHub, speedrun.com...).
// Le projet n'a pas de bibliothèque JSON. Ce petit lecteur comprend pourtant
// tout le format (objets, tableaux, textes avec leurs codes d'échappement,
// nombres, true/false/null) : un texte qui contient des guillemets ou des
// accolades ne risque pas de le tromper, contrairement à une simple
// recherche de texte. Il construit un arbre de valeurs qu'on parcourt ensuite.

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    std::string text;               // String : le texte décodé ; Number / Bool : tel qu'écrit
    std::vector<std::string> keys;  // Object : les noms des champs...
    std::vector<Value> items;       // ... et leurs valeurs (Array : les éléments)
    // [C++17] Un std::vector peut contenir le type qu'on est en train de
    // définir (Value contient des Value) : c'est ce qui permet l'arbre.

    // Le champ "key" d'un objet (nullptr s'il n'existe pas).
    const Value* get(std::string_view key) const
    {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }
    // Le texte du champ "key", ou "" s'il manque (ou n'est pas un texte).
    std::string string(std::string_view key) const
    {
        const Value* v = get(key);
        return v && v->type == Type::String ? v->text : std::string();
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_s(text) {}

    bool parse(Value& out)
    {
        if (!value(out, 0))
            return false;
        skipSpaces();
        return m_i == m_s.size(); // rien d'autre après
    }

private:
    void skipSpaces()
    {
        while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r'))
            ++m_i;
    }
    bool literal(std::string_view word)
    {
        if (m_s.substr(m_i, word.size()) != word)
            return false;
        m_i += word.size();
        return true;
    }

    bool value(Value& out, int depth)
    {
        if (depth > 64) // un JSON piégé, imbriqué à l'infini, ne fait pas déborder la pile
            return false;
        skipSpaces();
        if (m_i >= m_s.size())
            return false;
        const char c = m_s[m_i];
        if (c == '{')
            return object(out, depth);
        if (c == '[')
            return array(out, depth);
        if (c == '"') {
            out.type = Value::Type::String;
            return string(out.text);
        }
        if (c == 't' || c == 'f') {
            out.type = Value::Type::Bool;
            out.text = c == 't' ? "true" : "false";
            return literal(out.text);
        }
        if (c == 'n') {
            out.type = Value::Type::Null;
            return literal("null");
        }
        // Un nombre : on le garde tel quel (on n'en lit aucun).
        const size_t start = m_i;
        while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '-' ||
                                    m_s[m_i] == '+' || m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E'))
            ++m_i;
        if (m_i == start)
            return false;
        out.type = Value::Type::Number;
        out.text = std::string(m_s.substr(start, m_i - start));
        return true;
    }

    bool object(Value& out, int depth)
    {
        out.type = Value::Type::Object;
        ++m_i; // '{'
        skipSpaces();
        if (m_i < m_s.size() && m_s[m_i] == '}') {
            ++m_i;
            return true;
        }
        for (;;) {
            skipSpaces();
            std::string key;
            if (m_i >= m_s.size() || m_s[m_i] != '"' || !string(key))
                return false;
            skipSpaces();
            if (m_i >= m_s.size() || m_s[m_i] != ':')
                return false;
            ++m_i;
            Value item;
            if (!value(item, depth + 1))
                return false;
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(item));
            skipSpaces();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == '}') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    bool array(Value& out, int depth)
    {
        out.type = Value::Type::Array;
        ++m_i; // '['
        skipSpaces();
        if (m_i < m_s.size() && m_s[m_i] == ']') {
            ++m_i;
            return true;
        }
        for (;;) {
            Value item;
            if (!value(item, depth + 1))
                return false;
            out.items.push_back(std::move(item));
            skipSpaces();
            if (m_i < m_s.size() && m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_i < m_s.size() && m_s[m_i] == ']') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    // 4 chiffres hexadécimaux après "\u".
    bool hex4(unsigned& out)
    {
        if (m_i + 4 > m_s.size())
            return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = m_s[m_i++];
            out <<= 4;
            if (h >= '0' && h <= '9') out |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') out |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') out |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }

    // Écrit le caractère numéro "code" (Unicode) en UTF-8 : 1 à 4 octets.
    static void appendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string& out)
    {
        ++m_i; // '"'
        while (m_i < m_s.size()) {
            const char c = m_s[m_i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c; // (l'UTF-8 passe tel quel)
                continue;
            }
            if (m_i >= m_s.size())
                return false;
            const char e = m_s[m_i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!hex4(code))
                    return false;
                // Les caractères au-delà de U+FFFF (émojis...) s'écrivent en
                // deux morceaux, "🎮" : une "paire de substitution".
                if (code >= 0xD800 && code <= 0xDBFF && m_s.substr(m_i, 2) == "\\u") {
                    m_i += 2;
                    unsigned low = 0;
                    if (!hex4(low))
                        return false;
                    if (low >= 0xDC00 && low <= 0xDFFF)
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false; // texte jamais refermé
    }

    std::string_view m_s;
    size_t m_i = 0;
};

} // namespace json
