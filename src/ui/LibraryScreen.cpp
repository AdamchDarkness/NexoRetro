// Bibliothèque : filtres par catégorie, recherche, tri et grille de jaquettes.

#include "Screens.hpp"
#include "Widgets.hpp"

#include <algorithm>
#include <cctype>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Category {
    std::string id;
    std::string label;
    ImU32 dot;
};

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

    // Jeux visibles : catégorie + recherche, puis tri.
    std::vector<const Game*> games;
    const std::string needle = lower(m_search);
    // La recherche porte aussi sur ce que le scraper a trouvé : "combat",
    // "Capcom" ou "1995" retrouvent les jeux correspondants.
    auto matches = [&](const Game& g) {
        if (needle.empty() || lower(g.title).find(needle) != std::string::npos)
            return true;
        const scrape::GameInfo* info = ctx.scraper.find(g.path);
        return info && (lower(info->genre + " " + info->developer + " " + info->publisher).find(needle) != std::string::npos ||
                        (info->year && std::to_string(info->year) == needle));
    };
    for (const Game& g : all)
        if (inCategory(ctx, g, m_filter) && matches(g))
            games.push_back(&g);
    auto record = [&](const Game* g) { return ctx.userData.find(g->path); };
    if (m_sort == 1 || m_filter == "recent")
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) {
            return (record(a) ? record(a)->lastPlayed : 0) > (record(b) ? record(b)->lastPlayed : 0);
        });
    else if (m_sort == 2)
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) {
            return (record(a) ? record(a)->playSeconds : 0) > (record(b) ? record(b)->playSeconds : 0);
        });

    // --- En-tête : titre à gauche, recherche et tri à droite.
    const ImVec2 headerPos = ImGui::GetCursorPos();
    std::string subtitle = std::to_string(games.size()) + (games.size() > 1 ? " jeux" : " jeu");
    if (const int pending = ctx.scraper.pending(); pending > 0)
        subtitle += " · recherche des jaquettes en ligne (" + std::to_string(pending) + ")";
    pageTitle("Bibliothèque", subtitle);
    const ImVec2 afterTitle = ImGui::GetCursorPos();
    static const char* const sorts[] = {"Titre (A → Z)", "Dernière partie", "Temps de jeu"};
    const float searchW = 300.0f;
    const float sortW = 190.0f;
    ImGui::SetCursorPos(ImVec2(headerPos.x + width - px(searchW + 10.0f + sortW), headerPos.y + px(18.0f)));
    searchField("##search", m_search, sizeof(m_search), "Rechercher un jeu", searchW, ctx.nav.focusSearch);
    ctx.nav.focusSearch = false;
    sameLine();
    combo("##sort", sorts, &m_sort, sortW);
    ImGui::SetCursorPos(ImVec2(afterTitle.x, afterTitle.y + px(14.0f)));

    // --- Puces de catégorie, qui passent à la ligne si besoin.
    const float rowStart = ImGui::GetCursorPosX();
    for (size_t i = 0; i < categories.size(); ++i) {
        const Category& c = categories[i];
        int n = 0;
        for (const Game& g : all)
            n += inCategory(ctx, g, c.id) ? 1 : 0;
        const std::string count = std::to_string(n);
        if (i > 0) {
            const float w = chipWidth(c.label.c_str(), count.c_str());
            ImGui::SameLine(0.0f, px(8.0f));
            if (ImGui::GetCursorPosX() + w > rowStart + width) {
                ImGui::NewLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(0.0f));
            }
        }
        ImGui::PushID(c.id.c_str());
        if (chip("##chip", c.label.c_str(), count.c_str(), c.dot, m_filter == c.id))
            m_filter = c.id;
        ImGui::PopID();
    }
    space(10.0f);

    // --- Grille de jaquettes, dans une zone qui défile.
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);
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
            text("Essayez une autre catégorie ou une autre recherche.", 14.0f, Weight::Medium, theme::Muted);
        }
    }
    const float gridW = ImGui::GetContentRegionAvail().x - px(12.0f); // place pour la barre de défilement
    const float gapX = px(22.0f);
    const int columns = std::max(2, static_cast<int>((gridW + gapX) / (px(170.0f) + gapX)));
    const float tileW = (gridW - gapX * (columns - 1)) / columns;
    const float tileH = tileW * 1.36f;
    for (size_t i = 0; i < games.size(); ++i) {
        if (i % columns != 0)
            ImGui::SameLine(0.0f, gapX);
        else if (i > 0)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(18.0f));
        else
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6.0f)); // place pour l'effet de survol
        const Game& g = *games[i];
        const GameRecord* r = ctx.userData.find(g.path);
        const TileResult t = coverTile(ctx, g, tileW, tileH, formatAgo(r ? r->lastPlayed : 0), 18.0f);
        if (t.doubleClicked && ctx.library.hasCore(g))
            ctx.play(g, {});
        else if (t.clicked)
            ctx.nav.openGame(g);
    }
    space(20.0f);
    ImGui::EndChild();
}

} // namespace ui
