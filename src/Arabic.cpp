#include "Arabic.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace arabic {

namespace {

// --- UTF-8 <-> points de code ---------------------------------------------------

std::u32string decode(std::string_view s)
{
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out += cp;
    }
    return out;
}

std::string encode(const std::u32string& s)
{
    std::string out;
    for (const char32_t cp : s) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// --- Formes liées ----------------------------------------------------------------------

// Pour chaque lettre de U+0621 à U+064A : sa forme isolée (U+FExx). Les
// autres formes suivent dans l'ordre finale, initiale, médiane. joins :
// 2 = se lie des deux côtés, 1 = seulement à la lettre d'avant (alif, dal,
// ra, waw...), 0 = jamais (hamza). 0 en "isolée" = pas une lettre.
struct Letter {
    char16_t isolated;
    uint8_t joins;
};
constexpr Letter kLetters[] = {
    {0xFE80, 0}, // 0621 ء
    {0xFE81, 1}, // 0622 آ
    {0xFE83, 1}, // 0623 أ
    {0xFE85, 1}, // 0624 ؤ
    {0xFE87, 1}, // 0625 إ
    {0xFE89, 2}, // 0626 ئ
    {0xFE8D, 1}, // 0627 ا
    {0xFE8F, 2}, // 0628 ب
    {0xFE93, 1}, // 0629 ة
    {0xFE95, 2}, // 062A ت
    {0xFE99, 2}, // 062B ث
    {0xFE9D, 2}, // 062C ج
    {0xFEA1, 2}, // 062D ح
    {0xFEA5, 2}, // 062E خ
    {0xFEA9, 1}, // 062F د
    {0xFEAB, 1}, // 0630 ذ
    {0xFEAD, 1}, // 0631 ر
    {0xFEAF, 1}, // 0632 ز
    {0xFEB1, 2}, // 0633 س
    {0xFEB5, 2}, // 0634 ش
    {0xFEB9, 2}, // 0635 ص
    {0xFEBD, 2}, // 0636 ض
    {0xFEC1, 2}, // 0637 ط
    {0xFEC5, 2}, // 0638 ظ
    {0xFEC9, 2}, // 0639 ع
    {0xFECD, 2}, // 063A غ
    {0, 0},      // 063B
    {0, 0},      // 063C
    {0, 0},      // 063D
    {0, 0},      // 063E
    {0, 0},      // 063F
    {0x0640, 3}, // 0640 ـ (tatweel : se lie des deux côtés, une seule forme)
    {0xFED1, 2}, // 0641 ف
    {0xFED5, 2}, // 0642 ق
    {0xFED9, 2}, // 0643 ك
    {0xFEDD, 2}, // 0644 ل
    {0xFEE1, 2}, // 0645 م
    {0xFEE5, 2}, // 0646 ن
    {0xFEE9, 2}, // 0647 ه
    {0xFEED, 1}, // 0648 و
    {0xFEEF, 1}, // 0649 ى
    {0xFEF1, 2}, // 064A ي
};

const Letter* letterOf(char32_t c)
{
    if (c < 0x0621 || c > 0x064A)
        return nullptr;
    const Letter& l = kLetters[c - 0x0621];
    return l.isolated ? &l : nullptr;
}

// Les signes (voyelles courtes...) se placent sur une lettre sans couper la liaison.
bool transparent(char32_t c)
{
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670;
}

bool joinsNext(const Letter* l) { return l && (l->joins == 2 || l->joins == 3); }
bool joinsPrevious(const Letter* l) { return l && l->joins >= 1; }

std::u32string shape(const std::u32string& in)
{
    std::u32string out;
    const size_t n = in.size();
    // La lettre (non transparente) voisine, avant ou après la position i.
    auto neighbour = [&](size_t i, int step) -> const Letter* {
        for (long j = static_cast<long>(i) + step; j >= 0 && j < static_cast<long>(n); j += step) {
            if (transparent(in[static_cast<size_t>(j)]))
                continue;
            return letterOf(in[static_cast<size_t>(j)]);
        }
        return nullptr;
    };
    for (size_t i = 0; i < n; ++i) {
        const char32_t c = in[i];
        const Letter* l = letterOf(c);
        if (!l || l->joins == 3) {
            out += c;
            continue;
        }
        const Letter* before = neighbour(i, -1);
        const bool linkedBefore = joinsNext(before) && joinsPrevious(l);

        // Lam suivi d'un alif : une seule ligature.
        if (c == 0x0644) {
            size_t j = i + 1;
            while (j < n && transparent(in[j]))
                ++j;
            if (j < n) {
                char16_t ligature = 0;
                switch (in[j]) {
                case 0x0622: ligature = 0xFEF5; break;
                case 0x0623: ligature = 0xFEF7; break;
                case 0x0625: ligature = 0xFEF9; break;
                case 0x0627: ligature = 0xFEFB; break;
                default: break;
                }
                if (ligature) {
                    out += static_cast<char32_t>(ligature + (linkedBefore ? 1 : 0)); // +1 : forme finale
                    i = j;
                    continue;
                }
            }
        }

        const Letter* after = neighbour(i, +1);
        const bool linkedAfter = joinsNext(l) && joinsPrevious(after);
        int form = 0; // 0 isolée, 1 finale, 2 initiale, 3 médiane
        if (linkedBefore && linkedAfter)
            form = 3;
        else if (linkedBefore)
            form = 1;
        else if (linkedAfter)
            form = 2;
        out += static_cast<char32_t>(l->isolated + form);
    }
    return out;
}

// --- Ordre d'affichage -----------------------------------------------------------------

enum class Dir { L, R, N };

Dir directionOf(char32_t c)
{
    if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF))
        return (c >= 0x0660 && c <= 0x0669) ? Dir::L : Dir::R; // chiffres arabes : comme des nombres
    // Ponctuation et symboles (…, —, →, ●) : neutres.
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F))
        return Dir::N;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x00C0)
        return Dir::L;
    return Dir::N; // espaces, ponctuation, symboles
}

// Les parenthèses s'inversent dans un morceau écrit de droite à gauche.
char32_t mirror(char32_t c)
{
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    case 0x00AB: return 0x00BB; // « »
    case 0x00BB: return 0x00AB;
    default: return c;
    }
}

std::u32string reorder(const std::u32string& in)
{
    const size_t n = in.size();
    std::vector<Dir> dirs(n);
    for (size_t i = 0; i < n; ++i)
        dirs[i] = directionOf(in[i]);
    // Un caractère neutre prend le sens de ses voisins s'ils sont tous deux
    // de gauche à droite ("PlayStation 2"), sinon celui du texte (droite à gauche).
    for (size_t i = 0; i < n; ++i) {
        if (dirs[i] != Dir::N)
            continue;
        Dir before = Dir::R, after = Dir::R;
        for (size_t j = i; j-- > 0;)
            if (dirs[j] != Dir::N) {
                before = dirs[j];
                break;
            }
        for (size_t j = i + 1; j < n; ++j)
            if (dirs[j] != Dir::N) {
                after = dirs[j];
                break;
            }
        dirs[i] = (before == Dir::L && after == Dir::L) ? Dir::N : Dir::R;
    }
    for (size_t i = 0; i < n; ++i)
        if (dirs[i] == Dir::N)
            dirs[i] = Dir::L; // (neutre entre deux morceaux de gauche à droite)

    // Les morceaux, du dernier au premier ; les lettres des morceaux
    // arabes, à l'envers.
    std::vector<std::u32string> runs;
    std::vector<bool> rtl;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && dirs[j] == dirs[i])
            ++j;
        runs.push_back(in.substr(i, j - i));
        rtl.push_back(dirs[i] == Dir::R);
        i = j;
    }
    std::u32string out;
    for (size_t k = runs.size(); k-- > 0;) {
        if (rtl[k]) {
            for (size_t m = runs[k].size(); m-- > 0;)
                out += mirror(runs[k][m]);
        } else {
            out += runs[k];
        }
    }
    return out;
}

} // namespace

bool hasArabic(std::string_view utf8)
{
    for (size_t i = 0; i + 1 < utf8.size(); ++i) {
        // U+0621 à U+064A en UTF-8 : D8 A1 à D9 8A.
        const unsigned char a = static_cast<unsigned char>(utf8[i]);
        const unsigned char b = static_cast<unsigned char>(utf8[i + 1]);
        if ((a == 0xD8 && b >= 0xA1) || (a == 0xD9 && b <= 0x8A))
            return true;
    }
    return false;
}

std::string visual(std::string_view utf8)
{
    if (!hasArabic(utf8))
        return std::string(utf8);
    // Ligne par ligne : chaque ligne se lit de droite à gauche, mais les
    // lignes restent dans l'ordre.
    std::string out;
    size_t start = 0;
    while (true) {
        const size_t end = utf8.find('\n', start);
        const std::string_view line = utf8.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        out += encode(reorder(shape(decode(line))));
        if (end == std::string_view::npos)
            break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

} // namespace arabic
