#include "Themes.hpp"

#include "../Settings.hpp"
#include "../Utf8.hpp"
#include "Covers.hpp"

#include <SDL3/SDL.h>
#include <imgui_internal.h> // ImGui::ShadeVertsLinearColorGradientKeepAlpha

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace theme {

namespace {

constexpr ImU32 kBlack = IM_COL32(0, 0, 0, 255);

// Les teintes profondes des maquettes pour les jaquettes générées : chaque
// jeu en reçoit une, toujours la même (calculée à partir de son titre).
const std::vector<ImU32> kDefaultCovers = {
    rgb(0x1E2A78), rgb(0x8C1D2E), rgb(0x1F7A47), rgb(0xB3400B), rgb(0x4C3FB8), rgb(0x0F766E), rgb(0x9F1239),
    rgb(0x6D28D9), rgb(0x2563A8), rgb(0xB91C1C), rgb(0x334155), rgb(0x0E7490), rgb(0x7C2D12), rgb(0x3F4A5A)};

// --- Les thèmes intégrés -------------------------------------------------------
// [C++] Les "initialiseurs désignés" (.ground = ...) du C++20 : comme en C99,
// on nomme chaque champ. Ils doivent suivre l'ordre de la déclaration.

ThemeInfo lightTheme()
{
    ThemeInfo t;
    t.id = "clair";
    t.name = "Clair";
    t.palette = {.ground = rgb(0xF4F4F1), .surface = rgb(0xFFFFFF), .surfaceAlt = rgb(0xFAFAF8), .ink = rgb(0x111114),
                 .inkHover = rgb(0x2A2A30), .body = rgb(0x3A3A42), .muted = rgb(0x5E5E66), .hairline = rgb(0xE4E4DF),
                 .divider = rgb(0xECECE8), .dashed = rgb(0xC9C9C2), .track = rgb(0xD6D6D0), .placeholder = rgb(0xDCDDE6),
                 .accent = rgb(0x1F4FFF), .accentHover = rgb(0x1238C9), .accentSoft = rgb(0xEEF2FF),
                 .accentInk = rgb(0x1E2A78), .onAccent = rgb(0xFFFFFF), .success = rgb(0x15753F),
                 .successSoft = rgb(0xE3F4EA), .warning = rgb(0x8A3D06), .warningSoft = rgb(0xFFF1E0),
                 .danger = rgb(0xA3211B), .dangerSoft = rgb(0xFDE8E7), .neutralSoft = rgb(0xF0F0EC),
                 .favorite = rgb(0xE5484D), .coverInk = rgb(0xFFFFFF)};
    return t;
}

ThemeInfo darkTheme()
{
    ThemeInfo t;
    t.id = "sombre";
    t.name = "Sombre";
    t.palette = {.ground = rgb(0x16171B), .surface = rgb(0x212329), .surfaceAlt = rgb(0x292C33), .ink = rgb(0xF2F3F5),
                 .inkHover = rgb(0xD9DBE0), .body = rgb(0xCDCFD5), .muted = rgb(0x9EA1AA), .hairline = rgb(0x363940),
                 .divider = rgb(0x2D3037), .dashed = rgb(0x4D515B), .track = rgb(0x41454F), .placeholder = rgb(0x30333B),
                 .accent = rgb(0x5B7CFF), .accentHover = rgb(0x7A95FF), .accentSoft = rgb(0x262F52),
                 .accentInk = rgb(0xC0CDFF), .onAccent = rgb(0xFFFFFF), .success = rgb(0x4ADE80),
                 .successSoft = rgb(0x163322), .warning = rgb(0xFBBF24), .warningSoft = rgb(0x3A2D10),
                 .danger = rgb(0xF0605D), .dangerSoft = rgb(0x3D1E1E), .neutralSoft = rgb(0x272A30),
                 .favorite = rgb(0xF0606A), .coverInk = rgb(0xFFFFFF)};
    return t;
}

ThemeInfo oledTheme()
{
    // Noir pur : sur un écran OLED, les pixels noirs sont éteints.
    ThemeInfo t;
    t.id = "oled";
    t.name = "Nuit OLED";
    t.palette = {.ground = rgb(0x000000), .surface = rgb(0x0E0E10), .surfaceAlt = rgb(0x161619), .ink = rgb(0xFFFFFF),
                 .inkHover = rgb(0xDDDDE2), .body = rgb(0xD0D0D6), .muted = rgb(0x93939C), .hairline = rgb(0x26262B),
                 .divider = rgb(0x1C1C20), .dashed = rgb(0x3C3C44), .track = rgb(0x33333A), .placeholder = rgb(0x1E1E23),
                 .accent = rgb(0x00D1B2), .accentHover = rgb(0x3DE0C8), .accentSoft = rgb(0x06302A),
                 .accentInk = rgb(0x6BF0DC), .onAccent = rgb(0x00140F), .success = rgb(0x34D399),
                 .successSoft = rgb(0x0B2A1E), .warning = rgb(0xFBBF24), .warningSoft = rgb(0x2B2208),
                 .danger = rgb(0xF05252), .dangerSoft = rgb(0x301010), .neutralSoft = rgb(0x141417),
                 .favorite = rgb(0xFF5C6C), .coverInk = rgb(0xFFFFFF)};
    return t;
}

ThemeInfo synthwaveTheme()
{
    // Les années 80 : violet nuit, rose et cyan néon, dégradé façon coucher
    // de soleil.
    ThemeInfo t;
    t.id = "synthwave";
    t.name = "Synthwave";
    t.palette = {.ground = rgb(0x1B1036), .surface = rgb(0x271848), .surfaceAlt = rgb(0x2F1E56), .ink = rgb(0xFDEEFF),
                 .inkHover = rgb(0xF4D2FF), .body = rgb(0xE6D0F5), .muted = rgb(0xBBA2D6), .hairline = rgb(0x412C6E),
                 .divider = rgb(0x36245F), .dashed = rgb(0x5E4794), .track = rgb(0x4C377C), .placeholder = rgb(0x372662),
                 .accent = rgb(0xFF3EA5), .accentHover = rgb(0xFF6EBD), .accentSoft = rgb(0x4B1B5A),
                 .accentInk = rgb(0xFFB8E0), .onAccent = rgb(0x1B0B2E), .success = rgb(0x5EF2C0),
                 .successSoft = rgb(0x173E3E), .warning = rgb(0xFFC857), .warningSoft = rgb(0x3E2A24),
                 .danger = rgb(0xFF5C7A), .dangerSoft = rgb(0x4A173C), .neutralSoft = rgb(0x2B1B4F),
                 .favorite = rgb(0xFF4D8D), .coverInk = rgb(0xFFFFFF)};
    t.background = Background::Gradient;
    t.gradientFrom = rgb(0x120A2E);
    t.gradientTo = rgb(0x3A0F4E);
    t.gradientAngle = 90;
    t.soundPack = 2; // arcade
    t.covers = {rgb(0x5B1A8C), rgb(0x8C1A6B), rgb(0x1A3B8C), rgb(0x0F5C73), rgb(0xA8326E), rgb(0x3D1A8C), rgb(0x6B1A4F)};
    return t;
}

ThemeInfo gameBoyTheme()
{
    // Les quatre verts de l'écran de la Game Boy (1989).
    ThemeInfo t;
    t.id = "gameboy";
    t.name = "Game Boy";
    t.palette = {.ground = rgb(0x8BAC0F), .surface = rgb(0x9BBC0F), .surfaceAlt = rgb(0x93B40F), .ink = rgb(0x0F380F),
                 .inkHover = rgb(0x1E4A1E), .body = rgb(0x0F380F), .muted = rgb(0x2A5A2C), .hairline = rgb(0x6F9010),
                 .divider = rgb(0x7FA00F), .dashed = rgb(0x306230), .track = rgb(0x6F9010), .placeholder = rgb(0x7FA00F),
                 .accent = rgb(0x306230), .accentHover = rgb(0x0F380F), .accentSoft = rgb(0x84A50F),
                 .accentInk = rgb(0x0F380F), .onAccent = rgb(0x9BBC0F), .success = rgb(0x0F380F),
                 .successSoft = rgb(0x84A50F), .warning = rgb(0x0F380F), .warningSoft = rgb(0x84A50F),
                 .danger = rgb(0x0F380F), .dangerSoft = rgb(0x84A50F), .neutralSoft = rgb(0x93B40F),
                 .favorite = rgb(0x0F380F), .coverInk = rgb(0x9BBC0F)};
    t.soundPack = 1; // 8 bits
    t.covers = {rgb(0x0F380F), rgb(0x306230), rgb(0x1E4A1E)};
    return t;
}

// --- Fichiers de thème ---------------------------------------------------------

// Les clés d'un fichier .ini et la couleur de la palette qu'elles règlent.
// [C++] "ImU32 Palette::*" est un "pointeur sur membre" : il désigne un
// champ de Palette (pas celui d'une palette précise). palette.*membre
// donne ce champ dans une palette donnée.
struct ColorKey {
    const char* name;
    ImU32 Palette::*member;
};
const ColorKey kKeys[] = {
    {"ground", &Palette::ground},         {"surface", &Palette::surface},       {"surfaceAlt", &Palette::surfaceAlt},
    {"ink", &Palette::ink},               {"inkHover", &Palette::inkHover},     {"body", &Palette::body},
    {"muted", &Palette::muted},           {"hairline", &Palette::hairline},     {"divider", &Palette::divider},
    {"dashed", &Palette::dashed},         {"track", &Palette::track},           {"placeholder", &Palette::placeholder},
    {"accentHover", &Palette::accentHover}, {"accentSoft", &Palette::accentSoft}, {"accentInk", &Palette::accentInk},
    {"onAccent", &Palette::onAccent},     {"success", &Palette::success},       {"successSoft", &Palette::successSoft},
    {"warning", &Palette::warning},       {"warningSoft", &Palette::warningSoft}, {"danger", &Palette::danger},
    {"dangerSoft", &Palette::dangerSoft}, {"neutralSoft", &Palette::neutralSoft}, {"favorite", &Palette::favorite},
    {"coverInk", &Palette::coverInk}};

std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
        s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    return s;
}

int soundPackFromName(const std::string& name)
{
    if (name == "8bits" || name == "8 bits")
        return 1;
    if (name == "arcade")
        return 2;
    return 0;
}

// Lit un fichier de thème. Renvoie false s'il ne contient rien d'utile.
bool readThemeFile(const fs::path& file, const std::vector<ThemeInfo>& known, ThemeInfo& out)
{
    std::ifstream in(file);
    if (!in)
        return false;
    // D'abord toutes les lignes "clé=valeur" ; "base" doit être connue avant le reste.
    std::vector<std::pair<std::string, std::string>> lines;
    std::string line, base = "clair";
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key == "base")
            base = value;
        lines.emplace_back(std::move(key), std::move(value));
    }
    for (const ThemeInfo& t : known)
        if (t.id == base)
            out = t;
    out.id = toUtf8(file.stem());
    out.name = out.id;
    // (L'accent est appliqué à la fin : ses teintes dépendent du fond.)
    std::string accent;
    for (const auto& [key, value] : lines) {
        ImU32 color = 0;
        if (key == "name" && !value.empty())
            out.name = value;
        else if (key == "accent")
            accent = value;
        else if (key == "background")
            out.background = value == "gradient" ? Background::Gradient : value == "image" ? Background::Image : Background::Solid;
        else if (key == "gradientFrom" && parseColor(value, color))
            out.gradientFrom = color;
        else if (key == "gradientTo" && parseColor(value, color))
            out.gradientTo = color;
        else if (key == "gradientAngle")
            out.gradientAngle = std::atoi(value.c_str());
        else if (key == "image" && !value.empty())
            out.image = file.parent_path() / fromUtf8(value);
        else if (key == "imageDim")
            out.imageDim = std::clamp(std::atoi(value.c_str()), 0, 90);
        else if (key == "font" && !value.empty())
            out.font = file.parent_path() / fromUtf8(value);
        else if (key == "sounds")
            out.soundPack = soundPackFromName(value);
        else if (key == "covers") {
            out.covers.clear();
            std::istringstream list(value);
            std::string item;
            while (std::getline(list, item, ','))
                if (parseColor(trim(item), color))
                    out.covers.push_back(color);
        } else {
            for (const ColorKey& k : kKeys)
                if (key == k.name && parseColor(value, color))
                    out.palette.*k.member = color; // [C++] le champ désigné par le pointeur sur membre
        }
    }
    if (ImU32 color = 0; parseColor(accent, color)) // [C++17] un "if" avec sa propre variable
        out.palette = withAccent(out.palette, color);
    return !lines.empty();
}

std::vector<ThemeInfo> g_themes = {lightTheme(), darkTheme(), oledTheme(), synthwaveTheme(), gameBoyTheme()};
ThemeInfo g_active = lightTheme();
fs::path g_dir;

// Chemin d'un fichier du dossier des thèmes (les réglages gardent des
// chemins relatifs : le dossier de données peut être déplacé).
fs::path resolve(const std::string& value)
{
    if (value.empty())
        return {};
    const fs::path p = fromUtf8(value);
    return p.is_absolute() ? p : g_dir / p;
}

// Le contraste entre deux couleurs (1 = aucun, 21 = noir sur blanc).
float contrast(ImU32 a, ImU32 b)
{
    const float la = luminance(a) + 0.05f;
    const float lb = luminance(b) + 0.05f;
    return std::max(la, lb) / std::min(la, lb);
}

// La couleur fg, rapprochée de "toward" juste ce qu'il faut pour atteindre
// le contraste voulu sur le fond bg (texte lisible quel que soit le fond).
ImU32 ensureContrast(ImU32 fg, ImU32 bg, float ratio, ImU32 toward)
{
    for (int step = 0; step <= 10; ++step) {
        const ImU32 c = mix(fg, toward, static_cast<float>(step) / 10.0f);
        if (contrast(c, bg) >= ratio)
            return c;
    }
    return toward;
}

} // namespace

void loadThemes(const fs::path& dir)
{
    g_dir = dir;
    g_themes = {lightTheme(), darkTheme(), oledTheme(), synthwaveTheme(), gameBoyTheme()};
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".ini")
            files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        ThemeInfo t;
        if (!readThemeFile(file, g_themes, t))
            continue;
        // Un fichier qui porte le nom d'un thème intégré le remplace.
        const auto same = std::find_if(g_themes.begin(), g_themes.end(), [&](const ThemeInfo& o) { return o.id == t.id; });
        if (same != g_themes.end())
            *same = t;
        else
            g_themes.push_back(t);
        SDL_Log("Thème chargé : %s (%s)", t.name.c_str(), toUtf8(file.filename()).c_str());
    }
}

const fs::path& themesDir()
{
    return g_dir;
}

const std::vector<ThemeInfo>& themes()
{
    return g_themes;
}

const ThemeInfo& findTheme(const std::string& id)
{
    for (const ThemeInfo& t : g_themes)
        if (t.id == id)
            return t;
    return g_themes.front();
}

const ThemeInfo& active()
{
    return g_active;
}

Palette withAccent(Palette p, ImU32 accent)
{
    const bool dark = luminance(p.ground) < 0.25f;
    p.accent = accent;
    p.accentHover = dark ? mix(accent, White, 0.18f) : mix(accent, kBlack, 0.2f);
    p.accentSoft = mix(p.surface, accent, dark ? 0.24f : 0.11f);
    p.accentInk = dark ? mix(accent, White, 0.6f) : mix(accent, kBlack, 0.5f);
    // Le texte posé sur l'accent : blanc ou presque noir, le plus lisible des deux.
    const ImU32 darkInk = mix(accent, kBlack, 0.85f);
    p.onAccent = contrast(accent, White) >= contrast(accent, darkInk) ? White : darkInk;
    return p;
}

Palette withGround(Palette p, ImU32 g)
{
    const bool dark = luminance(g) < 0.2f;
    const bool wasDark = luminance(p.ground) < 0.25f;
    // Fond foncé sur un thème clair (ou l'inverse) : les textes et les
    // couleurs d'état viennent du thème Sombre (ou Clair), sinon ils seraient
    // illisibles.
    if (dark != wasDark)
        p = (dark ? darkTheme() : lightTheme()).palette;
    const ImU32 toward = dark ? White : kBlack;
    p.ground = g;
    if (dark) {
        p.surface = mix(g, White, 0.07f);
        p.surfaceAlt = mix(g, White, 0.11f);
        p.neutralSoft = mix(g, White, 0.09f);
        p.hairline = mix(g, White, 0.16f);
        p.divider = mix(g, White, 0.11f);
        p.dashed = mix(g, White, 0.3f);
        p.track = mix(g, White, 0.24f);
        p.placeholder = mix(g, White, 0.18f);
    } else {
        p.surface = mix(g, White, 0.78f);
        p.surfaceAlt = mix(g, White, 0.55f);
        p.neutralSoft = mix(g, toward, 0.025f);
        p.hairline = mix(g, toward, 0.08f);
        p.divider = mix(g, toward, 0.05f);
        p.dashed = mix(g, toward, 0.2f);
        p.track = mix(g, toward, 0.14f);
        p.placeholder = mix(g, toward, 0.1f);
    }
    // Les textes restent lisibles, sur le fond comme sur les cartes.
    p.ink = ensureContrast(p.ink, g, 9.0f, toward);
    p.body = ensureContrast(ensureContrast(p.body, p.surface, 6.0f, toward), g, 6.0f, toward);
    p.muted = ensureContrast(ensureContrast(p.muted, p.surface, 3.8f, toward), g, 3.8f, toward);
    // Les fonds teintés suivent les nouvelles cartes.
    p.accentSoft = mix(p.surface, p.accent, dark ? 0.24f : 0.11f);
    p.successSoft = mix(p.surface, p.success, dark ? 0.2f : 0.12f);
    p.warningSoft = mix(p.surface, p.warning, dark ? 0.2f : 0.12f);
    p.dangerSoft = mix(p.surface, p.danger, dark ? 0.2f : 0.1f);
    return p;
}

void applySettings(const Settings& s)
{
    ThemeInfo t = findTheme(s.theme);
    Palette p = t.palette;
    ImU32 a = 0, b = 0;
    switch (s.background) {
    case 1: // une couleur unie
        t.background = Background::Solid;
        if (parseColor(s.groundColor, a))
            p = withGround(p, a);
        break;
    case 2: // un dégradé : les cartes prennent la teinte du milieu
        if (parseColor(s.gradientFrom, a) && parseColor(s.gradientTo, b)) {
            // Comme sur Discord, le dégradé est "accordé" au thème : assombri
            // pour un thème sombre, éclairci pour un thème clair, juste ce
            // qu'il faut pour que le texte reste lisible dessus.
            const bool dark = luminance(p.ground) < 0.25f;
            const ImU32 toward = dark ? kBlack : White;
            float k = 0.0f;
            while (k < 0.85f && (dark ? luminance(mix(mix(a, b, 0.5f), toward, k)) > 0.05f
                                      : luminance(mix(mix(a, b, 0.5f), toward, k)) < 0.55f))
                k += 0.05f;
            t.background = Background::Gradient;
            t.gradientFrom = mix(a, toward, k);
            t.gradientTo = mix(b, toward, k);
            t.gradientAngle = s.gradientAngle;
            p = withGround(p, mix(t.gradientFrom, t.gradientTo, 0.5f));
        }
        break;
    case 3: // une image
        if (!s.backgroundImage.empty()) {
            t.background = Background::Image;
            t.image = resolve(s.backgroundImage);
            t.imageDim = std::clamp(s.backgroundDim, 0, 90);
        }
        break;
    default: // celui du thème
        break;
    }
    if (parseColor(s.accentColor, a))
        p = withAccent(p, a);
    if (!s.uiFont.empty())
        t.font = resolve(s.uiFont);
    t.palette = p;
    g_active = std::move(t);
    setPalette(p);
    setCustomFont(g_active.font);
}

ImU32 coverColor(size_t hash)
{
    const std::vector<ImU32>& list = g_active.covers.empty() ? kDefaultCovers : g_active.covers;
    return list[hash % list.size()];
}

void gradientRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 from, ImU32 to, int angleDeg, float rounding)
{
    // On dessine la forme en blanc, puis on recolore ses sommets selon leur
    // position le long de la direction du dégradé (fonction d'ImGui).
    const int first = dl->VtxBuffer.Size;
    dl->AddRectFilled(min, max, White, rounding);
    const int last = dl->VtxBuffer.Size;
    const float angle = static_cast<float>(angleDeg) * 3.14159265f / 180.0f;
    const ImVec2 dir(std::cos(angle), std::sin(angle));
    const ImVec2 center((min.x + max.x) / 2, (min.y + max.y) / 2);
    // La demi-longueur du rectangle mesurée dans cette direction : le
    // dégradé commence pile sur un coin et finit sur le coin opposé.
    const float half = (std::abs(dir.x) * (max.x - min.x) + std::abs(dir.y) * (max.y - min.y)) / 2;
    const ImVec2 p0(center.x - dir.x * half, center.y - dir.y * half);
    const ImVec2 p1(center.x + dir.x * half, center.y + dir.y * half);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, last, p0, p1, from, to);
}

void drawBackground(ImDrawList* dl, ImVec2 min, ImVec2 max, const Covers& covers)
{
    const ThemeInfo& t = g_active;
    dl->AddRectFilled(min, max, Ground);
    if (t.background == Background::Gradient) {
        gradientRect(dl, min, max, t.gradientFrom, t.gradientTo, t.gradientAngle);
    } else if (t.background == Background::Image) {
        // L'image remplit la fenêtre, puis un voile de la couleur du fond la
        // calme pour que le texte reste lisible.
        if (covers.drawImage(dl, min, max, toUtf8(t.image), 0.0f, 2560))
            dl->AddRectFilled(min, max, withAlpha(Ground, t.imageDim * 255 / 100));
    }
}

bool parseColor(const std::string& text, ImU32& out)
{
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    out = rgb(static_cast<unsigned>(std::stoul(hex, nullptr, 16)));
    return true;
}

std::string formatColor(ImU32 color)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", (color >> IM_COL32_R_SHIFT) & 0xFF, (color >> IM_COL32_G_SHIFT) & 0xFF,
                  (color >> IM_COL32_B_SHIFT) & 0xFF);
    return text;
}

} // namespace theme
