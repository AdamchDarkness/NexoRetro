// Accueil : "Reprendre la partie", jeux récents et consoles.

#include "Screens.hpp"
#include "Widgets.hpp"

#include <algorithm>
#include <system_error>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

// Les jeux joués, du plus récent au plus ancien.
std::vector<const Game*> recentGames(const UiContext& ctx)
{
    std::vector<const Game*> result;
    for (const Game& g : ctx.library.games()) {
        const GameRecord* r = ctx.userData.find(g.path);
        if (r && r->lastPlayed > 0)
            result.push_back(&g);
    }
    std::sort(result.begin(), result.end(), [&](const Game* a, const Game* b) {
        return ctx.userData.find(a->path)->lastPlayed > ctx.userData.find(b->path)->lastPlayed;
    });
    return result;
}

// Une ligne "sauvegarde d'état" : vignette grise + nom + date.
bool stateRow(const char* id, const char* name, long long time, float width)
{
    const bool exists = time > 0;
    ImGui::BeginDisabled(!exists);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, px(62.0f)), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    draw::rect(dl, min, max, hovered ? theme::Ground : theme::SurfaceAlt, px(14.0f), theme::Divider);
    const ImVec2 thumbMin(min.x + px(8.0f), min.y + px(9.0f));
    draw::rect(dl, thumbMin, ImVec2(thumbMin.x + px(64.0f), thumbMin.y + px(44.0f)),
               exists ? theme::Placeholder : theme::NeutralSoft, px(8.0f));
    if (exists)
        draw::icon(dl, draw::Icon::Play, ImVec2(thumbMin.x + px(23.0f), thumbMin.y + px(13.0f)), px(18.0f),
                   theme::withAlpha(theme::Ink, 120));
    draw::text(dl, ImVec2(min.x + px(84.0f), min.y + px(13.0f)), Weight::Bold, 13.0f, theme::Ink, name);
    draw::text(dl, ImVec2(min.x + px(84.0f), min.y + px(32.0f)), Weight::Medium, 12.0f, theme::Muted,
               exists ? formatAgo(time) : "Vide");
    return clicked && exists;
}

void drawResume(UiContext& ctx, const Game* game, float width)
{
    beginCard("##resume", ImVec2(width, px(290.0f)), 28.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 inner = ImGui::GetContentRegionAvail();

    if (!game) {
        // Premier lancement : pas encore de partie à reprendre.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(30.0f));
        overline("BIENVENUE");
        text("Votre salle d'arcade personnelle", 40.0f, Weight::ExtraBold);
        textWrapped("Ajoutez vos jeux et les BIOS de vos consoles : l'émulateur reconnaît tout seul la console de chaque "
                    "jeu. Votre dernière partie apparaîtra ici.",
                    15.0f, Weight::Medium, theme::Body, px(620.0f));
        space(8.0f);
        if (button("Ajouter des jeux##welcome", {.style = ButtonStyle::Primary, .icon = draw::Icon::Plus, .height = 48.0f, .fontSize = 15.0f}))
            ctx.nav.go(Screen::AddGames);
        sameLine();
        if (button("Gérer les BIOS##welcome", {.style = ButtonStyle::Outline, .icon = draw::Icon::Chip, .height = 48.0f, .fontSize = 15.0f}))
            ctx.nav.go(Screen::Bios);
        endCard();
        return;
    }

    const GameRecord* record = ctx.userData.find(game->path);
    const ImVec2 origin = ImGui::GetCursorPos(); // coin haut-gauche, marges comprises
    const float coverW = px(180.0f);
    const ImVec2 coverMin = ImGui::GetCursorScreenPos();
    const ImVec2 coverMax(coverMin.x + coverW, coverMin.y + inner.y);
    draw::shadow(dl, coverMin, coverMax, px(16.0f), px(16.0f), px(10.0f), 40);
    ctx.covers.draw(dl, coverMin, coverMax, *game, ctx.userData, px(16.0f), 24.0f);
    if (ImGui::InvisibleButton("##resumeCover", ImVec2(coverW, inner.y), ImGuiButtonFlags_EnableNav))
        ctx.nav.openGame(*game);

    // Colonne d'informations, centrée verticalement.
    const float infoX = coverW + px(28.0f);
    const float statesW = px(260.0f);
    const float infoW = inner.x - infoX - statesW - px(28.0f);
    const float top = origin.y;
    ImGui::SetCursorPos(ImVec2(origin.x + infoX, top + px(34.0f)));
    ImGui::BeginGroup();
    overline("REPRENDRE LA PARTIE");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(2.0f));
    const ImVec2 titlePos = ImGui::GetCursorScreenPos();
    draw::textEllipsis(dl, titlePos, Weight::ExtraBold, 40.0f, theme::Ink, game->title, infoW);
    ImGui::Dummy(ImVec2(infoW, px(52.0f)));
    pill(game->system->name, Tone::Accent);
    sameLine(8.0f);
    pill(formatAgo(record ? record->lastPlayed : 0).c_str(), Tone::Info);
    if (record && record->playSeconds > 0) {
        sameLine(8.0f);
        pill((formatDuration(record->playSeconds) + " de jeu").c_str(), Tone::Info);
    }
    space(10.0f);
    const std::filesystem::path autoState = autoStateFile(ctx, *game);
    const bool canPlay = ctx.library.hasCore(*game);
    if (button("Reprendre##resume", {.style = ButtonStyle::Primary, .icon = draw::Icon::Play, .height = 48.0f,
                                     .fontSize = 15.0f, .disabled = !canPlay,
                                     .tooltip = canPlay ? nullptr : "L'émulateur de cette console est manquant"}))
        ctx.play(*game, fileTime(autoState) > 0 ? autoState : std::filesystem::path());
    sameLine();
    button("Jouer avec un ami##resume", {.style = ButtonStyle::Outline, .icon = draw::Icon::Users, .height = 48.0f,
                                        .fontSize = 15.0f, .disabled = true,
                                        .tooltip = "Le jeu en ligne arrive dans une prochaine version"});
    ImGui::EndGroup();

    // Sauvegardes d'état, à droite.
    ImGui::SetCursorPos(ImVec2(origin.x + inner.x - statesW, top));
    ImGui::BeginGroup();
    overline("SAUVEGARDES D'ÉTAT");
    space(2.0f);
    if (stateRow("##auto", "Automatique", fileTime(autoState), statesW))
        ctx.play(*game, autoState);
    space(2.0f);
    const std::filesystem::path quick = quickStateFile(ctx, *game);
    if (stateRow("##quick", "Rapide (F2)", fileTime(quick), statesW))
        ctx.play(*game, quick);
    ImGui::EndGroup();
    endCard();
}

} // namespace

void HomeScreen::draw(UiContext& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // (Le panneau "Amis" reviendra avec la liste d'amis : voir le README.)
    const float leftW = avail.x;
    const std::vector<const Game*> recents = recentGames(ctx);

    ImGui::BeginChild("##homeLeft", ImVec2(leftW, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawResume(ctx, recents.empty() ? nullptr : recents.front(), leftW);
    space(6.0f);

    // Jeux récents (ou, au début, les premiers jeux de la bibliothèque).
    const bool hasRecents = !recents.empty();
    std::vector<const Game*> shelf = recents;
    if (!hasRecents)
        for (const Game& g : ctx.library.games())
            shelf.push_back(&g);
    if (sectionHeader(hasRecents ? "Jeux récents" : "Dans votre bibliothèque", "Tout voir", leftW)) {
        ctx.nav.go(Screen::Library);
        ctx.nav.librarySystem = hasRecents ? "recent" : "all";
    }
    space(4.0f);
    const float tileW = px(140.0f);
    const float gap = px(20.0f);
    const int count = std::max(1, static_cast<int>((leftW + gap) / (tileW + gap)));
    if (shelf.empty()) {
        text("Vos jeux apparaîtront ici dès que vous en aurez ajouté.", 14.0f, Weight::Medium, theme::Muted);
        space(150.0f);
    }
    for (int i = 0; i < std::min(count, static_cast<int>(shelf.size())); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, gap);
        const Game& g = *shelf[i];
        const GameRecord* r = ctx.userData.find(g.path);
        const TileResult t = coverTile(ctx, g, tileW, px(172.0f), formatAgo(r ? r->lastPlayed : 0), 16.0f);
        if (t.doubleClicked && ctx.library.hasCore(g))
            ctx.play(g, {});
        else if (t.clicked)
            ctx.nav.openGame(g);
    }
    space(4.0f);

    // Consoles qui ont au moins un jeu.
    if (sectionHeader("Consoles", "Gérer les BIOS", leftW))
        ctx.nav.go(Screen::Bios);
    space(4.0f);
    std::vector<const System*> systems;
    for (const System& s : Library::systems())
        if (ctx.library.countFor(s) > 0)
            systems.push_back(&s);
    if (systems.empty())
        for (const System& s : Library::systems())
            systems.push_back(&s);
    const int columns = 6;
    const float tileGap = px(14.0f);
    const float sysW = (leftW - tileGap * (columns - 1)) / columns;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < std::min(columns, static_cast<int>(systems.size())); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, tileGap);
        const System& s = *systems[i];
        ImGui::PushID(s.id);
        if (ImGui::InvisibleButton("##system", ImVec2(sysW, px(88.0f)), ImGuiButtonFlags_EnableNav)) {
            ctx.nav.go(Screen::Library);
            ctx.nav.librarySystem = s.id;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin();
        draw::rect(dl, min, ImVec2(min.x + sysW, min.y + px(88.0f)), hovered ? theme::SurfaceAlt : theme::Surface, px(16.0f));
        draw::rect(dl, ImVec2(min.x + px(16.0f), min.y + px(14.0f)), ImVec2(min.x + px(40.0f), min.y + px(38.0f)),
                   theme::rgb(s.color), px(8.0f));
        draw::textEllipsis(dl, ImVec2(min.x + px(16.0f), min.y + px(46.0f)), Weight::Bold, 14.0f, theme::Ink, s.name,
                           sysW - px(32.0f));
        const int n = ctx.library.countFor(s);
        draw::text(dl, ImVec2(min.x + px(16.0f), min.y + px(65.0f)), Weight::Medium, 12.0f, theme::Muted,
                   std::to_string(n) + (n > 1 ? " jeux" : " jeu"));
    }
    ImGui::EndChild();
}

} // namespace ui
