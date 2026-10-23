// Bibliothèque : catégories, recherche, filtres (genre, année, joueurs,
// éditeur), tri, et six façons de voir ses jeux : grille de jaquettes,
// liste détaillée, carrousel, boîtes 3D, cartouches ou CD.

#include "../Lang.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <imgui_internal.h> // ImGui::ShadeVertsLinearColorGradientKeepAlpha

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

// Pour comparer sans se soucier des majuscules ni des accents :
// "Pokémon" et "pokemon" deviennent tous les deux "pokemon".
std::string fold(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // Les lettres accentuées les plus courantes (UTF-8 : 0xC3 puis un octet).
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]) | 0x20; // majuscule -> minuscule
            const char* base = nullptr;
            if (d >= 0xA0 && d <= 0xA5) base = "a";
            else if (d == 0xA7) base = "c";
            else if (d >= 0xA8 && d <= 0xAB) base = "e";
            else if (d >= 0xAC && d <= 0xAF) base = "i";
            else if (d == 0xB1) base = "n";
            else if (d >= 0xB2 && d <= 0xB6) base = "o";
            else if (d >= 0xB9 && d <= 0xBC) base = "u";
            if (base) {
                out += base;
                ++i;
                continue;
            }
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

struct Category {
    std::string id;
    std::string label;
    ImU32 dot;
};

// La largeur utile de la zone des jeux, barre de défilement COMPRISE. Si on
// prenait seulement la place restante (GetContentRegionAvail), elle
// changerait selon que la barre est affichée ou non : la grille perdrait
// une colonne, la page raccourcirait, la barre disparaîtrait... et tout
// recommencerait à l'image suivante (la page clignote).
float stableWidth()
{
    return ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize - px(12.0f);
}

bool inCategory(const UiContext& ctx, const Game& g, const std::string& id)
{
    const GameRecord* r = ctx.userData.find(g.path);
    if (id == "all")
        return true;
    if (id == "fav")
        return r && r->favorite;
    if (id == "recent")
        return r && r->lastPlayed > 0;
    return id == g.system->id;
}

// "Action, Platform" -> {"Action", "Platform"}
std::vector<std::string> genresOf(const std::string& genre)
{
    std::vector<std::string> list;
    std::istringstream in(genre);
    std::string part;
    while (std::getline(in, part, ',')) {
        while (!part.empty() && part.front() == ' ')
            part.erase(part.begin());
        while (!part.empty() && part.back() == ' ')
            part.pop_back();
        if (!part.empty())
            list.push_back(part);
    }
    return list;
}

// Une liste déroulante "Tous les genres / Action / ..." : renvoie true si
// le choix a changé. current : la valeur choisie (vide = tout).
bool filterCombo(const char* id, const char* allLabel, const std::set<std::string>& values, std::string& current, float width)
{
    std::vector<const char*> items = {allLabel};
    int index = 0;
    for (const std::string& v : values) {
        if (v == current)
            index = static_cast<int>(items.size());
        items.push_back(v.c_str());
    }
    if (!combo(id, items, &index, width))
        return false;
    current = index == 0 ? std::string() : std::string(items[static_cast<size_t>(index)]);
    return true;
}

// --- Vignettes en objets : boîtes 3D, cartouches, CD ---------------------------------
// Pas de vraie 3D ici : des formes plates (quadrilatères, cercles) et des
// dégradés suffisent à donner l'illusion du relief.

enum class TileStyle { Box, Cartridge, Disc };

// Recolore en dégradé (de c0 en p0 à c1 en p1) tout ce qui a été dessiné
// depuis le sommet n° first. Garde la transparence de chaque sommet.
void shade(ImDrawList* dl, int first, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, dl->VtxBuffer.Size, p0, p1, c0, c1);
}

// Une boîte de jeu posée de biais : la tranche à gauche fuit vers
// l'arrière (plus courte), la jaquette en face avant, un reflet de plastique.
void drawBox(const UiContext& ctx, ImDrawList* dl, const Game& game, ImVec2 min, ImVec2 max)
{
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float spine = w * 0.11f; // largeur de la tranche
    const float skew = h * 0.035f; // de combien elle rétrécit en haut et en bas
    const ImVec2 front(min.x + spine, min.y);
    draw::shadow(dl, ImVec2(min.x + spine * 0.5f, min.y + px(4.0f)), max, px(4.0f), px(14.0f), px(9.0f), 70);

    // La tranche : la couleur de la jaquette, assombrie, et une bande de la
    // couleur de la console. edgeY : la hauteur d'un point de la tranche
    // (f = 0 en haut, 1 en bas), à gauche (au fond) ou à droite (devant).
    const ImU32 base = Covers::colorFor(game);
    auto edgeY = [&](bool left, float f) { return left ? min.y + skew + f * (h - 2 * skew) : min.y + f * h; };
    dl->AddQuadFilled(ImVec2(min.x, edgeY(true, 0)), front, ImVec2(front.x, max.y), ImVec2(min.x, edgeY(true, 1)),
                      theme::mix(base, IM_COL32_BLACK, 0.45f));
    dl->AddQuadFilled(ImVec2(min.x, edgeY(true, 0.07f)), ImVec2(front.x, edgeY(false, 0.07f)),
                      ImVec2(front.x, edgeY(false, 0.15f)), ImVec2(min.x, edgeY(true, 0.15f)),
                      theme::rgb(game.system->color));
    // La face avant : la jaquette (avec son titre si elle est générée).
    ctx.covers.draw(dl, front, max, game, ctx.userData, px(3.0f), 16.0f);
    // Le reflet du boîtier : un voile blanc dans le coin en haut à gauche.
    dl->AddRectFilledMultiColor(front, max, theme::withAlpha(theme::White, 60), theme::withAlpha(theme::White, 0),
                                theme::withAlpha(theme::White, 0), theme::withAlpha(theme::White, 18));
    // L'arête entre la tranche et la face, qui accroche la lumière.
    dl->AddLine(front, ImVec2(front.x, max.y), theme::withAlpha(theme::White, 90), px(1.0f));
}

// Une cartouche : coque en plastique au coin coupé, stries pour les doigts,
// la jaquette en étiquette, et la fente du connecteur en bas.
void drawCartridge(const UiContext& ctx, ImDrawList* dl, const Game& game, ImVec2 min, ImVec2 max)
{
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    const float cut = w * 0.14f;
    const float bevel = px(5.0f);
    draw::shadow(dl, min, max, bevel, px(14.0f), px(8.0f), 60);
    // La coque : un polygone convexe (le coin coupé en haut à droite).
    const ImU32 plastic = theme::mix(theme::rgb(0x70737D), theme::rgb(game.system->color), 0.18f);
    const int first = dl->VtxBuffer.Size;
    dl->PathLineTo(ImVec2(min.x + bevel, min.y));
    dl->PathLineTo(ImVec2(max.x - cut, min.y));
    dl->PathLineTo(ImVec2(max.x, min.y + cut));
    dl->PathLineTo(ImVec2(max.x, max.y - bevel));
    dl->PathLineTo(ImVec2(max.x - bevel, max.y));
    dl->PathLineTo(ImVec2(min.x + bevel, max.y));
    dl->PathLineTo(ImVec2(min.x, max.y - bevel));
    dl->PathLineTo(ImVec2(min.x, min.y + bevel));
    dl->PathFillConvex(theme::White);
    // Éclairée par le haut : plus claire en haut qu'en bas.
    shade(dl, first, min, ImVec2(min.x, max.y), theme::mix(plastic, theme::White, 0.18f), theme::mix(plastic, IM_COL32_BLACK, 0.25f));
    // Les stries du haut.
    const ImU32 groove = theme::mix(plastic, IM_COL32_BLACK, 0.3f);
    for (int i = 0; i < 3; ++i) {
        const float y = min.y + h * (0.045f + 0.03f * static_cast<float>(i));
        dl->AddLine(ImVec2(min.x + w * 0.16f, y), ImVec2(max.x - w * 0.28f, y), groove, px(1.5f));
    }
    // L'étiquette, posée dans un léger creux.
    const ImVec2 labelMin(min.x + w * 0.1f, min.y + h * 0.17f);
    const ImVec2 labelMax(max.x - w * 0.1f, max.y - h * 0.2f);
    draw::rect(dl, ImVec2(labelMin.x - px(3.0f), labelMin.y - px(3.0f)), ImVec2(labelMax.x + px(3.0f), labelMax.y + px(3.0f)),
               theme::mix(plastic, IM_COL32_BLACK, 0.22f), px(6.0f));
    ctx.covers.draw(dl, labelMin, labelMax, game, ctx.userData, px(4.0f), 14.0f);
    // La fente du connecteur, et un petit triangle en relief ("insérer ici").
    draw::rect(dl, ImVec2(min.x + w * 0.22f, max.y - h * 0.11f), ImVec2(max.x - w * 0.22f, max.y - h * 0.06f),
               theme::mix(plastic, IM_COL32_BLACK, 0.45f), px(3.0f));
    const float cx = (min.x + max.x) / 2;
    const float ty = max.y - h * 0.155f;
    dl->AddTriangleFilled(ImVec2(cx - px(5.0f), ty + px(3.0f)), ImVec2(cx + px(5.0f), ty + px(3.0f)), ImVec2(cx, ty - px(3.0f)),
                          theme::mix(plastic, theme::White, 0.3f));
    // Le bord éclairé de la coque.
    dl->AddLine(ImVec2(min.x + bevel, min.y + px(1.0f)), ImVec2(max.x - cut, min.y + px(1.0f)),
                theme::withAlpha(theme::White, 80), px(1.0f));
}

// Un CD : disque argenté aux reflets irisés, la jaquette en étiquette ronde,
// le moyeu transparent et le trou central.
void drawDisc(const UiContext& ctx, ImDrawList* dl, const Game& game, ImVec2 min, ImVec2 max)
{
    const float r = std::min(max.x - min.x, max.y - min.y) / 2;
    const ImVec2 c((min.x + max.x) / 2, (min.y + max.y) / 2);
    draw::shadow(dl, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), r, px(14.0f), px(8.0f), 60);
    // Le disque argenté (dégradé en diagonale).
    int first = dl->VtxBuffer.Size;
    dl->AddCircleFilled(c, r, theme::White, 96);
    shade(dl, first, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), theme::rgb(0xEEF0F4), theme::rgb(0x8E949E));
    // Les reflets arc-en-ciel : des "parts de tarte" de couleur, très transparentes.
    static const unsigned kSheen[] = {0x7DD3FC, 0xF0ABFC, 0xFDE68A, 0x86EFAC};
    for (int i = 0; i < 4; ++i) {
        const float a0 = -0.6f + 0.35f * static_cast<float>(i);
        const float a1 = a0 + 0.3f;
        for (const float flip : {0.0f, 3.14159265f})
            dl->AddTriangleFilled(c, ImVec2(c.x + std::cos(a0 + flip) * r, c.y + std::sin(a0 + flip) * r),
                                  ImVec2(c.x + std::cos(a1 + flip) * r, c.y + std::sin(a1 + flip) * r),
                                  theme::rgb(kSheen[i], 70));
    }
    // L'étiquette : la jaquette découpée en rond (un arrondi égal à la
    // moitié du carré donne un cercle), ou la jaquette générée.
    const float label = r * 0.94f;
    const ImVec2 lmin(c.x - label, c.y - label), lmax(c.x + label, c.y + label);
    if (!ctx.covers.drawArt(dl, lmin, lmax, game, ctx.userData, label)) {
        const ImU32 color = Covers::colorFor(game);
        first = dl->VtxBuffer.Size;
        dl->AddCircleFilled(c, label, theme::White, 96);
        shade(dl, first, lmin, lmax, theme::mix(color, theme::White, 0.12f), theme::mix(color, IM_COL32_BLACK, 0.2f));
        // Le nom de la console en haut, le titre en bas, centrés.
        const float tagSize = std::max(9.0f, r / theme::scale() * 0.11f);
        const ImVec2 ts = draw::measure(Weight::Bold, tagSize, game.system->shortName);
        draw::text(dl, ImVec2(c.x - ts.x / 2, c.y - r * 0.62f), Weight::Bold, tagSize, theme::withAlpha(theme::CoverInk, 215),
                   game.system->shortName);
        const float titleSize = std::max(11.0f, r / theme::scale() * 0.13f);
        const float maxW = r * 1.25f;
        const float tw = std::min(maxW, draw::measure(Weight::ExtraBold, titleSize, game.title).x);
        draw::textEllipsis(dl, ImVec2(c.x - tw / 2, c.y + r * 0.42f), Weight::ExtraBold, titleSize, theme::CoverInk, game.title, maxW);
    }
    dl->AddCircle(c, label, theme::withAlpha(IM_COL32_BLACK, 40), 96, px(1.0f));
    // Le moyeu transparent, son anneau, et le trou (qui laisse voir le fond).
    dl->AddCircleFilled(c, r * 0.3f, theme::withAlpha(theme::rgb(0xDDE1E7), 235), 48);
    dl->AddCircle(c, r * 0.3f, theme::withAlpha(IM_COL32_BLACK, 35), 48, px(1.0f));
    dl->AddCircle(c, r * 0.2f, theme::withAlpha(theme::White, 160), 48, px(1.0f));
    dl->AddCircleFilled(c, r * 0.11f, theme::Ground, 32);
    dl->AddCircle(c, r * 0.11f, theme::withAlpha(IM_COL32_BLACK, 50), 32, px(1.0f));
}

// Une vignette cliquable en objet, avec titre et sous-titre dessous (comme
// coverTile, Common.cpp).
TileResult objectTile(const UiContext& ctx, const Game& game, float width, float artH, TileStyle style,
                      const std::string& subtitle)
{
    TileResult result;
    const float textBlock = px(42.0f);
    ImGui::PushID(&game);
    result.clicked = ImGui::InvisibleButton("##tile", ImVec2(width, artH + px(10.0f) + textBlock));
    const bool hovered = ImGui::IsItemHovered();
    result.doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // Au survol, l'objet monte un peu.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float lift = storage->GetFloat(key, 0.0f);
    lift += ((hovered ? 1.0f : 0.0f) - lift) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    storage->SetFloat(key, lift);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetItemRectMin();
    const ImVec2 min(pos.x, pos.y - px(6.0f) * lift);
    const ImVec2 max(min.x + width, min.y + artH);
    switch (style) {
    case TileStyle::Box: drawBox(ctx, dl, game, min, max); break;
    case TileStyle::Cartridge: drawCartridge(ctx, dl, game, min, max); break;
    case TileStyle::Disc: drawDisc(ctx, dl, game, min, max); break;
    }
    const float y = pos.y + artH + px(10.0f);
    draw::textEllipsis(dl, ImVec2(pos.x, y), Weight::Bold, 14.0f, theme::Ink, game.title, width);
    draw::textEllipsis(dl, ImVec2(pos.x, y + px(20.0f)), Weight::Medium, 12.0f, theme::Muted, subtitle, width);
    return result;
}

} // namespace

void LibraryScreen::draw(UiContext& ctx)
{
    // Une autre page peut demander un filtre précis (ex. une console).
    if (!ctx.nav.librarySystem.empty()) {
        m_filter = ctx.nav.librarySystem;
        ctx.nav.librarySystem.clear();
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const auto& all = ctx.library.games();

    // Catégories : fixes, puis une par console qui a des jeux.
    std::vector<Category> categories = {{"all", "Tous", theme::Ink},
                                        {"fav", "Favoris", theme::Favorite},
                                        {"recent", "Récents", theme::Accent}};
    for (const System& s : Library::systems())
        if (ctx.library.countFor(s) > 0)
            categories.push_back({s.id, s.name, theme::rgb(s.color)});

    // Les valeurs possibles des filtres, d'après ce que le scraper a trouvé
    // sur les jeux de la catégorie choisie.
    std::set<std::string> genres, publishers;
    std::set<int> decades;
    for (const Game& g : all) {
        if (!inCategory(ctx, g, m_filter))
            continue;
        if (const scrape::GameInfo* info = ctx.scraper.find(g.path)) {
            for (const std::string& genre : genresOf(info->genre))
                genres.insert(genre);
            if (!info->publisher.empty())
                publishers.insert(info->publisher);
            if (info->year > 0)
                decades.insert(info->year / 10 * 10);
        }
    }

    // Recherche : chaque mot doit se trouver dans le titre, la console, le
    // genre, le développeur, l'éditeur ou l'année ("mario snes", "capcom
    // 1995"...). Sans tenir compte des majuscules ni des accents.
    std::vector<std::string> words;
    {
        std::istringstream in(fold(m_search));
        std::string word;
        while (in >> word)
            words.push_back(word);
    }
    auto matches = [&](const Game& g) {
        const scrape::GameInfo* info = ctx.scraper.find(g.path);
        // Les filtres.
        if (!m_genre.empty()) {
            const std::vector<std::string> list = info ? genresOf(info->genre) : std::vector<std::string>{};
            if (std::find(list.begin(), list.end(), m_genre) == list.end())
                return false;
        }
        if (!m_publisher.empty() && (!info || info->publisher != m_publisher))
            return false;
        if (m_decade > 0 && (!info || info->year / 10 * 10 != m_decade))
            return false;
        if (m_players > 0 && (!info || info->players < m_players))
            return false;
        if (words.empty())
            return true;
        std::string haystack = g.title + " " + g.system->name + " " + g.system->shortName;
        if (info)
            haystack += " " + info->genre + " " + info->developer + " " + info->publisher + " " +
                        (info->year ? std::to_string(info->year) : std::string());
        haystack = fold(haystack);
        for (const std::string& w : words)
            if (haystack.find(w) == std::string::npos)
                return false;
        return true;
    };

    std::vector<const Game*> games;
    for (const Game& g : all)
        if (inCategory(ctx, g, m_filter) && matches(g))
            games.push_back(&g);
    auto record = [&](const Game* g) { return ctx.userData.find(g->path); };
    auto year = [&](const Game* g) {
        const scrape::GameInfo* info = ctx.scraper.find(g->path);
        return info && info->year > 0 ? info->year : 9999; // inconnue : à la fin
    };
    if (m_sort == 1 || m_filter == "recent")
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) {
            return (record(a) ? record(a)->lastPlayed : 0) > (record(b) ? record(b)->lastPlayed : 0);
        });
    else if (m_sort == 2)
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) {
            return (record(a) ? record(a)->playSeconds : 0) > (record(b) ? record(b)->playSeconds : 0);
        });
    else if (m_sort == 3)
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) { return year(a) < year(b); });

    // --- En-tête : titre et nombre de jeux à gauche ; à droite, seulement la
    // recherche, le bouton « Filtres » et le choix de la vue.
    const ImVec2 headerPos = ImGui::GetCursorPos();
    std::string subtitle = std::to_string(games.size()) + (games.size() > 1 ? " jeux" : " jeu");
    if (const int pending = ctx.scraper.pending(); pending > 0)
        subtitle += " · recherche des jaquettes en ligne (" + std::to_string(pending) + ")";
    pageTitle("Bibliothèque", subtitle);
    const ImVec2 afterTitle = ImGui::GetCursorPos();

    const int activeFilters = (m_genre.empty() ? 0 : 1) + (m_publisher.empty() ? 0 : 1) + (m_decade > 0 ? 1 : 0) +
                              (m_players > 0 ? 1 : 0);
    const float searchW = 300.0f;
    const char* filtersLabel = "Filtres";
    const float filtersW = draw::measure(Weight::Bold, 14.0f, filtersLabel).x / theme::scale() + 18.0f * 2 + 17.0f + 8.0f;
    const float viewsW = 68.0f; // le bouton « Vue » (icône de la vue + flèche)
    ImGui::SetCursorPos(
        ImVec2(headerPos.x + width - px(searchW + 10.0f + filtersW + 10.0f + viewsW), headerPos.y + px(18.0f)));
    searchField("##search", m_search, sizeof(m_search), "Rechercher un jeu", searchW, ctx.nav.focusSearch);
    ctx.nav.focusSearch = false;
    sameLine();
    if (button("Filtres##open", {.style = activeFilters > 0 ? ButtonStyle::Primary : ButtonStyle::Outline,
                                 .icon = draw::Icon::Filter, .height = 38.0f, .tooltip = "Trier et filtrer"}) ||
        ctx.nav.openFilters) {
        ImGui::OpenPopup("##filters");
        ctx.nav.openFilters = false;
    }
    const ImVec2 filtersMin = ImGui::GetItemRectMin();
    const ImVec2 filtersMax = ImGui::GetItemRectMax();
    if (activeFilters > 0) {
        // Une pastille bleue avec le nombre de filtres actifs.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(filtersMax.x - px(4.0f), filtersMin.y + px(4.0f));
        dl->AddCircleFilled(c, px(10.0f), theme::Accent);
        const std::string n = std::to_string(activeFilters);
        const ImVec2 ns = draw::measure(Weight::Bold, 11.0f, n);
        draw::text(dl, ImVec2(c.x - ns.x / 2, c.y - ns.y / 2), Weight::Bold, 11.0f, theme::OnAccent, n);
    }
    sameLine();
    drawViewSwitch(ctx);
    drawFiltersPopup(genres, publishers, decades, ImVec2(filtersMax.x, filtersMax.y + px(8.0f)));
    ImGui::SetCursorPos(ImVec2(afterTitle.x, afterTitle.y + px(14.0f)));

    // --- Puces de catégorie, sur une seule ligne qui défile (molette).
    {
        const float chipsH = px(42.0f);
        ImGui::BeginChild("##chips", ImVec2(width, chipsH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        for (size_t i = 0; i < categories.size(); ++i) {
            const Category& c = categories[i];
            int n = 0;
            for (const Game& g : all)
                n += inCategory(ctx, g, c.id) ? 1 : 0;
            const std::string count = std::to_string(n);
            if (i > 0)
                ImGui::SameLine(0.0f, px(8.0f));
            ImGui::PushID(c.id.c_str());
            if (chip("##chip", c.label.c_str(), count.c_str(), c.dot, m_filter == c.id))
                m_filter = c.id;
            ImGui::PopID();
        }
        // La molette (verticale ou horizontale) fait défiler les puces.
        if (ImGui::IsWindowHovered()) {
            const ImGuiIO& io = ImGui::GetIO();
            const float wheel = io.MouseWheel != 0.0f ? io.MouseWheel : io.MouseWheelH;
            if (wheel != 0.0f)
                ImGui::SetScrollX(ImGui::GetScrollX() - wheel * px(90.0f));
        }
        // Un fondu sur les bords quand des puces sont cachées.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wMin = ImGui::GetWindowPos();
        const ImU32 clear = theme::withAlpha(theme::Ground, 0);
        if (ImGui::GetScrollX() < ImGui::GetScrollMaxX() - 1.0f)
            dl->AddRectFilledMultiColor(ImVec2(wMin.x + width - px(48.0f), wMin.y), ImVec2(wMin.x + width, wMin.y + chipsH), clear,
                                        theme::Ground, theme::Ground, clear);
        if (ImGui::GetScrollX() > 1.0f)
            dl->AddRectFilledMultiColor(wMin, ImVec2(wMin.x + px(48.0f), wMin.y + chipsH), theme::Ground, clear, clear,
                                        theme::Ground);
        ImGui::EndChild();
    }

    // --- Les filtres actifs, en pastilles supprimables (rien s'il n'y en a pas).
    if (activeFilters > 0) {
        space(4.0f);
        bool first = true;
        auto removable = [&](const std::string& label) {
            if (!first)
                sameLine(8.0f);
            first = false;
            return removablePill(label);
        };
        if (!m_genre.empty() && removable(m_genre))
            m_genre.clear();
        if (m_decade > 0 && removable(m_decade < 2000 ? "Années " + std::to_string(m_decade % 100)
                                                      : "Années " + std::to_string(m_decade)))
            m_decade = 0;
        if (m_players > 0 && removable(std::to_string(m_players) + (m_players > 1 ? " joueurs et +" : " joueur et +")))
            m_players = 0;
        if (!m_publisher.empty() && removable(m_publisher))
            m_publisher.clear();
        sameLine(14.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(7.0f));
        if (link("Tout effacer", 13.0f))
            resetFilters();
    }
    space(10.0f);

    // --- Les jeux, dans une zone qui défile.
    // (Le carrousel tient dans la page : pas de défilement, la molette sert
    // à passer d'un jeu à l'autre.)
    const bool carousel = ctx.settings.libraryView == 2 && !games.empty();
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None,
                      carousel ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav
                               : ImGuiWindowFlags_None);
    if (games.empty()) {
        space(30.0f);
        if (all.empty()) {
            text("Aucun jeu pour l'instant", 22.0f, Weight::ExtraBold);
            text("Ajoutez vos jeux : leur console est reconnue automatiquement.", 14.0f, Weight::Medium, theme::Muted);
            space(6.0f);
            if (button("Ajouter des jeux##empty", {.style = ButtonStyle::Primary, .icon = draw::Icon::Plus}))
                ctx.nav.go(Screen::AddGames);
        } else {
            text("Aucun jeu ne correspond", 18.0f, Weight::ExtraBold);
            text("Essayez une autre catégorie, une autre recherche ou d'autres filtres.", 14.0f, Weight::Medium, theme::Muted);
        }
    } else if (ctx.settings.libraryView == 1) {
        drawList(ctx, games);
    } else if (ctx.settings.libraryView == 2) {
        drawCarousel(ctx, games);
    } else {
        drawGrid(ctx, games);
    }
    if (!carousel)
        space(20.0f);
    ImGui::EndChild();
}

// --- En-tête : vues, filtres -------------------------------------------------------------

void LibraryScreen::resetFilters()
{
    m_genre.clear();
    m_publisher.clear();
    m_decade = 0;
    m_players = 0;
    m_sort = 0;
}

// Les six vues, dans le même ordre que Settings::libraryView.
struct LibraryView {
    draw::Icon icon;
    const char* label;
};
constexpr LibraryView kViews[] = {{draw::Icon::Grid, "Grille de jaquettes"}, {draw::Icon::List, "Liste détaillée"},
                                  {draw::Icon::Carousel, "Carrousel"},       {draw::Icon::Box, "Boîtes 3D"},
                                  {draw::Icon::Cartridge, "Cartouches"},     {draw::Icon::Disc, "CD"}};

const char* libraryViewName(int view)
{
    return kViews[std::clamp(view, 0, static_cast<int>(std::size(kViews)) - 1)].label;
}

// Le bouton « Vue » : l'icône de la vue choisie et une flèche. Il ouvre un
// petit menu avec les six vues (l'en-tête reste léger).
void LibraryScreen::drawViewSwitch(UiContext& ctx)
{
    const int current = std::clamp(ctx.settings.libraryView, 0, static_cast<int>(std::size(kViews)) - 1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 size = px(68.0f, 38.0f);
    const bool clicked = ImGui::InvisibleButton("##viewMenu", size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", std::string(lang::tr("Vue")).c_str());
    const ImVec2 p = ImGui::GetItemRectMin();
    draw::rect(dl, p, ImVec2(p.x + size.x, p.y + size.y), hovered ? theme::SurfaceAlt : theme::Surface, size.y / 2,
               theme::Hairline);
    draw::icon(dl, kViews[current].icon, ImVec2(p.x + px(14.0f), p.y + px(10.0f)), px(18.0f), theme::Ink);
    draw::icon(dl, draw::Icon::ChevronDown, ImVec2(p.x + px(38.0f), p.y + px(11.0f)), px(16.0f), theme::Muted);
    if (clicked || ctx.nav.openViews) {
        ImGui::OpenPopup("##views");
        ctx.nav.openViews = false;
    }

    // Le menu, aligné à droite sous le bouton.
    const float menuW = px(250.0f);
    ImGui::SetNextWindowPos(ImVec2(p.x + size.x - menuW, p.y + size.y + px(8.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(menuW, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(0.0f, 2.0f));
    if (ImGui::BeginPopup("##views")) {
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        for (int i = 0; i < static_cast<int>(std::size(kViews)); ++i) {
            if (i == 3) {
                // Un trait entre les vues "à plat" et les vues en objets.
                const ImVec2 q = ImGui::GetCursorScreenPos();
                pdl->AddLine(ImVec2(q.x + px(10.0f), q.y + px(4.0f)), ImVec2(q.x + menuW - px(26.0f), q.y + px(4.0f)),
                             theme::Divider, px(1.0f));
                ImGui::Dummy(px(1.0f, 8.0f));
            }
            ImGui::PushID(i);
            const bool picked = ImGui::InvisibleButton("##view", ImVec2(menuW - px(16.0f), px(40.0f)));
            const bool over = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 m = ImGui::GetItemRectMin();
            const bool selected = i == current;
            if (selected || over)
                draw::rect(pdl, m, ImVec2(m.x + menuW - px(16.0f), m.y + px(40.0f)),
                           selected ? theme::AccentSoft : theme::SurfaceAlt, px(12.0f));
            const ImU32 color = selected ? theme::AccentInk : theme::Ink;
            draw::icon(pdl, kViews[i].icon, ImVec2(m.x + px(12.0f), m.y + px(11.0f)), px(18.0f), color);
            const ImVec2 ts = draw::measure(Weight::Bold, 14.0f, kViews[i].label);
            draw::text(pdl, ImVec2(m.x + px(42.0f), m.y + (px(40.0f) - ts.y) / 2), Weight::Bold, 14.0f, color, kViews[i].label);
            if (selected)
                draw::icon(pdl, draw::Icon::Check, ImVec2(m.x + menuW - px(44.0f), m.y + px(11.0f)), px(18.0f), theme::Accent);
            if (picked) {
                ctx.settings.libraryView = i;
                ctx.settingsChanged();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

// La fenêtre « Trier et filtrer », sous le bouton Filtres (topRight : son coin).
void LibraryScreen::drawFiltersPopup(const std::set<std::string>& genres, const std::set<std::string>& publishers,
                                     const std::set<int>& decades, ImVec2 topRight)
{
    const float popupW = px(380.0f);
    ImGui::SetNextWindowPos(ImVec2(topRight.x - popupW, topRight.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(popupW, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(22.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopup("##filters")) {
        overline("TRIER ET FILTRER");
        space(2.0f);
        const float comboW = 220.0f;
        // Une ligne : le libellé à gauche, la liste déroulante à droite.
        auto row = [&](const char* label, auto&& control) {
            const float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(y + px(11.0f));
            text(label, 13.0f, Weight::Bold, theme::Muted);
            ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - px(comboW), y));
            control();
            space(2.0f);
        };
        static const char* const sorts[] = {"Titre (A → Z)", "Dernière partie", "Temps de jeu", "Année de sortie"};
        row("Trier par", [&] { combo("##sort", sorts, &m_sort, comboW); });
        row("Genre", [&] { filterCombo("##genre", "Tous les genres", genres, m_genre, comboW); });
        row("Année", [&] {
            std::vector<std::string> names = {"Toutes les années"};
            std::vector<int> values = {0};
            for (int d : decades) {
                names.push_back(d < 2000 ? "Années " + std::to_string(d % 100) : "Années " + std::to_string(d));
                values.push_back(d);
            }
            std::vector<const char*> items;
            int index = 0;
            for (size_t i = 0; i < names.size(); ++i) {
                items.push_back(names[i].c_str());
                if (values[i] == m_decade)
                    index = static_cast<int>(i);
            }
            if (combo("##decade", items, &index, comboW))
                m_decade = values[static_cast<size_t>(index)];
        });
        row("Joueurs", [&] {
            static const char* const players[] = {"Tous les joueurs", "1 joueur et +", "2 joueurs et +", "3 joueurs et +",
                                                  "4 joueurs et +"};
            combo("##players", players, &m_players, comboW);
        });
        row("Éditeur", [&] { filterCombo("##publisher", "Tous les éditeurs", publishers, m_publisher, comboW); });
        space(8.0f);
        if (button("Réinitialiser##filters", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Refresh, .height = 40.0f}))
            resetFilters();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - px(110.0f));
        if (button("Fermer##filters", {.style = ButtonStyle::Primary, .height = 40.0f, .width = 110.0f}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

// Une pastille « Action ✕ » : un clic la retire.
bool LibraryScreen::removablePill(const std::string& label)
{
    const ImVec2 ts = draw::measure(Weight::Bold, 13.0f, label);
    const ImVec2 size(ts.x + px(14.0f + 8.0f + 12.0f + 12.0f), px(32.0f));
    ImGui::PushID(label.c_str());
    const bool clicked = ImGui::InvisibleButton("##pill", size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    draw::rect(dl, min, ImVec2(min.x + size.x, min.y + size.y), hovered ? theme::AccentSoft : theme::Surface, size.y / 2,
               hovered ? theme::Accent : theme::Hairline);
    draw::text(dl, ImVec2(min.x + px(14.0f), min.y + (size.y - ts.y) / 2), Weight::Bold, 13.0f, theme::Ink, label);
    draw::icon(dl, draw::Icon::Close, ImVec2(min.x + size.x - px(26.0f), min.y + px(10.0f)), px(12.0f),
               hovered ? theme::Accent : theme::Muted);
    return clicked;
}

// --- Grille de jaquettes --------------------------------------------------------------

void LibraryScreen::drawGrid(UiContext& ctx, const std::vector<const Game*>& games)
{
    // La grille sert aussi aux vues en objets (3 = boîtes, 4 = cartouches,
    // 5 = CD) : seule la forme des vignettes change.
    const int view = ctx.settings.libraryView;
    const float gridW = stableWidth();
    const float gapX = px(view == 5 ? 26.0f : 22.0f);
    const int columns = std::max(2, static_cast<int>((gridW + gapX) / (px(170.0f) + gapX)));
    const float tileW = (gridW - gapX * (columns - 1)) / columns;
    const float tileH = tileW * (view == 4 ? 1.12f : view == 5 ? 1.0f : 1.36f);
    for (size_t i = 0; i < games.size(); ++i) {
        if (i % columns != 0)
            ImGui::SameLine(0.0f, gapX);
        else if (i > 0)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(18.0f));
        else
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6.0f)); // place pour l'effet de survol
        const Game& g = *games[i];
        const GameRecord* r = ctx.userData.find(g.path);
        const std::string subtitle = formatAgo(r ? r->lastPlayed : 0);
        TileResult t;
        if (view == 3)
            t = objectTile(ctx, g, tileW, tileH, TileStyle::Box, subtitle);
        else if (view == 4)
            t = objectTile(ctx, g, tileW, tileH, TileStyle::Cartridge, subtitle);
        else if (view == 5)
            t = objectTile(ctx, g, tileW, tileH, TileStyle::Disc, subtitle);
        else
            t = coverTile(ctx, g, tileW, tileH, subtitle, 18.0f);
        if (t.doubleClicked && ctx.library.hasCore(g))
            ctx.play(g, {});
        else if (t.clicked)
            ctx.nav.openGame(g);
    }
}

// --- Liste détaillée ------------------------------------------------------------------

void LibraryScreen::drawList(UiContext& ctx, const std::vector<const Game*>& games)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowW = stableWidth();
    const float rowH = px(72.0f);
    // Les colonnes : jaquette + titre | console | année | genre | temps de jeu | dernière partie.
    const float colSystem = rowW * 0.42f;
    const float colYear = rowW * 0.58f;
    const float colGenre = rowW * 0.65f;
    const float colTime = rowW * 0.80f;
    const float colLast = rowW * 0.90f;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        auto head = [&](float x, const char* label) {
            draw::text(dl, ImVec2(p.x + x, p.y), Weight::Bold, 11.0f, theme::Muted, label);
        };
        head(px(78.0f), "JEU");
        head(colSystem, "CONSOLE");
        head(colYear, "ANNÉE");
        head(colGenre, "GENRE");
        head(colTime, "TEMPS");
        head(colLast, "DERNIÈRE PARTIE");
        ImGui::Dummy(ImVec2(rowW, px(20.0f)));
    }

    // [C++] ImGuiListClipper : seules les lignes visibles sont dessinées,
    // même avec des milliers de jeux.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(games.size()), rowH + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Game& g = *games[static_cast<size_t>(i)];
            ImGui::PushID(&g);
            const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
            const bool hovered = ImGui::IsItemHovered();
            const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            ImGui::PopID();
            const ImVec2 p = ImGui::GetItemRectMin();
            if (hovered) {
                draw::rect(dl, p, ImVec2(p.x + rowW, p.y + rowH), theme::Surface, px(14.0f));
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            const float coverH = rowH - px(12.0f);
            ctx.covers.draw(dl, ImVec2(p.x + px(10.0f), p.y + px(6.0f)), ImVec2(p.x + px(10.0f) + coverH * 0.75f, p.y + px(6.0f) + coverH),
                            g, ctx.userData, px(6.0f), 0.0f);

            const scrape::GameInfo* info = ctx.scraper.find(g.path);
            const GameRecord* r = ctx.userData.find(g.path);
            const float textX = p.x + px(78.0f);
            const float midY = p.y + rowH / 2;
            draw::textEllipsis(dl, ImVec2(textX, midY - px(18.0f)), Weight::Bold, 15.0f, theme::Ink, g.title,
                               colSystem - px(90.0f));
            std::string sub = info && !info->publisher.empty() ? info->publisher : g.region;
            if (r && r->favorite)
                sub = (sub.empty() ? "" : sub + " · ") + "Favori";
            draw::textEllipsis(dl, ImVec2(textX, midY + px(2.0f)), Weight::Medium, 12.0f, theme::Muted, sub, colSystem - px(90.0f));

            auto cell = [&](float x, float w, const std::string& value) {
                draw::textEllipsis(dl, ImVec2(p.x + x, midY - px(9.0f)), Weight::SemiBold, 13.0f, theme::Body, value, w - px(12.0f));
            };
            // La pastille de couleur de la console, puis son nom.
            dl->AddCircleFilled(ImVec2(p.x + colSystem + px(5.0f), midY), px(4.0f), theme::rgb(g.system->color));
            cell(colSystem + px(16.0f), colYear - colSystem - px(16.0f), g.system->name);
            cell(colYear, colGenre - colYear, info && info->year ? std::to_string(info->year) : "—");
            cell(colGenre, colTime - colGenre, info && !info->genre.empty() ? info->genre : "—");
            cell(colTime, colLast - colTime, r && r->playSeconds > 0 ? formatDuration(r->playSeconds) : "—");
            cell(colLast, rowW - colLast, formatAgo(r ? r->lastPlayed : 0));

            if (doubleClicked && ctx.library.hasCore(g))
                ctx.play(g, {});
            else if (clicked)
                ctx.nav.openGame(g);
        }
    }
}

// --- Carrousel --------------------------------------------------------------------------

void LibraryScreen::drawCarousel(UiContext& ctx, const std::vector<const Game*>& games)
{
    const int count = static_cast<int>(games.size());
    m_carouselIndex = std::clamp(m_carouselIndex, 0, count - 1);

    // Flèches gauche/droite (clavier ou manette) et molette : jeu précédent / suivant.
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)))
        --m_carouselIndex;
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)))
        ++m_carouselIndex;
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        m_carouselIndex -= ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1;
    m_carouselIndex = std::clamp(m_carouselIndex, 0, count - 1);
    // La position affichée glisse doucement vers le jeu choisi.
    m_carouselPos += (static_cast<float>(m_carouselIndex) - m_carouselPos) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);
    if (std::abs(m_carouselPos - m_carouselIndex) > count)
        m_carouselPos = static_cast<float>(m_carouselIndex);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float areaW = ImGui::GetContentRegionAvail().x - px(12.0f);
    const float areaH = std::max(px(200.0f), ImGui::GetContentRegionAvail().y - px(160.0f));
    const float coverH = areaH * 0.86f;
    const float coverW = coverH * 0.75f;
    const float centerX = origin.x + areaW / 2;
    const float spacing = coverW * 0.62f;

    // Les jaquettes, des plus éloignées aux plus proches (la sélectionnée
    // par-dessus) : plus petites et plus pâles à mesure qu'on s'éloigne.
    const int first = std::max(0, static_cast<int>(m_carouselPos) - 5);
    const int last = std::min(count - 1, static_cast<int>(m_carouselPos) + 6);
    std::vector<int> order;
    for (int i = first; i <= last; ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::abs(a - m_carouselPos) > std::abs(b - m_carouselPos);
    });
    int clickedIndex = -1;
    for (int i : order) {
        const float offset = static_cast<float>(i) - m_carouselPos;
        const float distance = std::min(std::abs(offset), 4.0f);
        const float scale = 1.0f - 0.16f * distance;
        const float w = coverW * scale;
        const float h = coverH * scale;
        const float x = centerX + offset * spacing - w / 2;
        const float y = origin.y + (areaH - h) / 2;
        if (x + w < origin.x || x > origin.x + areaW)
            continue;
        const ImVec2 min(x, y), max(x + w, y + h);
        draw::shadow(dl, min, max, px(16.0f), px(18.0f), px(8.0f), distance < 0.5f ? 60 : 30);
        ctx.covers.draw(dl, min, max, *games[static_cast<size_t>(i)], ctx.userData, px(16.0f), 20.0f * scale);
        if (distance > 0.05f) // les jaquettes éloignées s'effacent dans le fond
            draw::rect(dl, min, max, theme::withAlpha(theme::Ground, static_cast<int>(std::min(1.0f, distance / 3.0f) * 200)),
                       px(16.0f));
        if (ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            clickedIndex = i;
    }
    ImGui::Dummy(ImVec2(areaW, areaH));
    const Game& selected = *games[static_cast<size_t>(m_carouselIndex)];
    if (clickedIndex >= 0) {
        if (clickedIndex == m_carouselIndex)
            ctx.nav.openGame(selected);
        else
            m_carouselIndex = clickedIndex;
    }

    // Sous le carrousel : le jeu choisi, ses informations et "Jouer".
    space(8.0f);
    const scrape::GameInfo* info = ctx.scraper.find(selected.path);
    std::string details = selected.system->name;
    if (info && info->year)
        details += " · " + std::to_string(info->year);
    if (info && !info->genre.empty())
        details += " · " + info->genre;
    if (info && !info->publisher.empty())
        details += " · " + info->publisher;
    const ImVec2 titleSize = draw::measure(Weight::ExtraBold, 26.0f, selected.title);
    const ImVec2 detailSize = draw::measure(Weight::Medium, 14.0f, details);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    draw::text(dl, ImVec2(origin.x + (areaW - std::min(titleSize.x, areaW)) / 2, p.y), Weight::ExtraBold, 26.0f, theme::Ink,
               selected.title);
    draw::text(dl, ImVec2(origin.x + (areaW - std::min(detailSize.x, areaW)) / 2, p.y + px(36.0f)), Weight::Medium, 14.0f,
               theme::Muted, details);
    ImGui::Dummy(ImVec2(areaW, px(64.0f)));
    const float playW = 150.0f, infoW = 150.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (areaW - px(playW + 10.0f + infoW)) / 2);
    const bool hasCore = selected.system->external() || ctx.library.hasCore(selected);
    if (button("Jouer##carousel", {.style = ButtonStyle::Primary, .icon = draw::Icon::Play, .height = 44.0f, .width = playW,
                                   .disabled = !hasCore, .tooltip = hasCore ? nullptr : "Le core de cette console n'est pas installé"}))
        ctx.play(selected, {});
    sameLine();
    if (button("Fiche du jeu##carousel", {.style = ButtonStyle::Outline, .height = 44.0f, .width = infoW}))
        ctx.nav.openGame(selected);
    // Entrée (ou A à la manette) : jouer au jeu choisi.
    if (!typing && hasCore && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)))
        ctx.play(selected, {});
}

} // namespace ui
