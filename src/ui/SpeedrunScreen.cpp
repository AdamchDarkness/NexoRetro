// L'écran Speedrun : un coin pour les speedrunners. Pour chaque jeu, ses
// catégories et leurs splits (modifiables), le record, la somme des
// meilleurs segments, les essais, l'historique des runs avec leur vidéo,
// et les réglages de l'enregistrement. Le chrono lui-même tourne en jeu
// (App::drawSpeedrunOverlay, touches F5 à F9).

#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"
#include "../Compat.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

namespace {

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string dateText(long long timestamp)
{
    const std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm day{};
    localtime_s(&day, &t);
    char text[32];
    std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", &day);
    return text;
}

// Une tuile de statistique : petit titre, grande valeur.
void statTile(ImDrawList* dl, ImVec2 p, float w, const char* label, const std::string& value, ImU32 color)
{
    draw::rect(dl, p, ImVec2(p.x + w, p.y + px(78.0f)), theme::Surface, px(16.0f), theme::Hairline);
    draw::text(dl, ImVec2(p.x + px(16.0f), p.y + px(14.0f)), Weight::Bold, 11.0f, theme::Muted, label);
    draw::textEllipsis(dl, ImVec2(p.x + px(16.0f), p.y + px(34.0f)), Weight::ExtraBold, 24.0f, color, value, w - px(24.0f));
}

} // namespace

void SpeedrunScreen::draw(UiContext& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float leftW = px(330.0f);
    const float rightW = avail.x - leftW - px(24.0f);
    // Le jeu affiché : le dernier choisi, sinon le premier qui a des runs.
    if (m_game.empty() && !ctx.speedrun.games().empty())
        m_game = fromUtf8(ctx.speedrun.games().begin()->first);

    // --- Colonne de gauche : les jeux, les touches.
    ImGui::BeginGroup();
    pageTitle("Speedrun", "Chrono, splits, records et vidéos de vos runs.");
    space(8.0f);
    const float listH = std::max(px(200.0f), avail.y - px(90.0f) - px(290.0f));
    beginCard("##srGames", ImVec2(leftW, listH), 18.0f, theme::Surface, 24.0f, true);
    text("Mes jeux", 16.0f, Weight::ExtraBold);
    space(2.0f);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& [key, runs] : ctx.speedrun.games()) {
        const fs::path path = fromUtf8(key);
        const Game* g = ctx.library.find(path);
        ImGui::PushID(key.c_str());
        const bool clicked = ImGui::InvisibleButton("##game", ImVec2(rowW, px(60.0f)), ImGuiButtonFlags_EnableNav);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 p = ImGui::GetItemRectMin();
        const bool selected = UserData::keyOf(m_game) == key;
        if (selected || hovered)
            draw::rect(dl, p, ImVec2(p.x + rowW, p.y + px(60.0f)), selected ? theme::AccentSoft : theme::Ground, px(12.0f));
        if (g)
            ctx.covers.draw(dl, ImVec2(p.x + px(8.0f), p.y + px(6.0f)), ImVec2(p.x + px(44.0f), p.y + px(54.0f)), *g,
                            ctx.userData, px(6.0f), 0.0f);
        const std::string title = g ? g->title : toUtf8(path.stem());
        draw::textEllipsis(dl, ImVec2(p.x + px(56.0f), p.y + px(12.0f)), Weight::Bold, 13.0f, theme::Ink, title, rowW - px(64.0f));
        const speedrun::Category* cat = runs.categories.empty()
                                            ? nullptr
                                            : &runs.categories[static_cast<size_t>(
                                                  std::clamp(runs.current, 0, static_cast<int>(runs.categories.size()) - 1))];
        const std::string sub = cat ? cat->name + " · PB " + speedrun::formatTime(cat->pb()) : std::string();
        draw::textEllipsis(dl, ImVec2(p.x + px(56.0f), p.y + px(32.0f)), Weight::Medium, 12.0f, theme::Muted, sub,
                           rowW - px(64.0f));
        if (clicked)
            m_game = path;
    }
    if (ctx.speedrun.games().empty())
        textWrapped("Choisissez un jeu pour préparer vos catégories et vos splits.", 13.0f, Weight::Medium, theme::Muted, rowW);
    space(6.0f);
    if (button("Choisir un jeu", {.style = ButtonStyle::Outline, .icon = draw::Icon::Plus, .height = 40.0f, .width = -1.0f}))
        m_openPicker = true;
    endCard();
    space(14.0f);
    // Les touches du chrono, toujours sous les yeux.
    beginCard("##srKeys", ImVec2(leftW, px(262.0f)), 18.0f);
    text("Pendant le jeu", 16.0f, Weight::ExtraBold);
    space(2.0f);
    const struct {
        const char* key;
        const char* what;
    } keys[] = {{"F5", "Départ, puis split suivant"},
                {"F6", "Annuler le dernier split"},
                {"F7", "Passer un split"},
                {"F9", "Remettre le chrono à zéro"}};
    const float keyW = ImGui::GetContentRegionAvail().x;
    for (const auto& k : keys) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* kdl = ImGui::GetWindowDrawList();
        draw::rect(kdl, p, ImVec2(p.x + px(40.0f), p.y + px(28.0f)), theme::Ground, px(8.0f), theme::Hairline);
        const ImVec2 ks = draw::measure(Weight::ExtraBold, 12.0f, k.key);
        draw::text(kdl, ImVec2(p.x + (px(40.0f) - ks.x) / 2, p.y + (px(28.0f) - ks.y) / 2), Weight::ExtraBold, 12.0f,
                   theme::Ink, k.key);
        draw::textEllipsis(kdl, ImVec2(p.x + px(52.0f), p.y + px(6.0f)), Weight::Medium, 13.0f, theme::Body, k.what,
                           keyW - px(52.0f));
        ImGui::Dummy(ImVec2(keyW, px(34.0f)));
    }
    textWrapped("Charger un état, rembobiner ou accélérer rend la run « assistée » : pas de record.", 12.0f, Weight::Medium,
                theme::Muted, keyW);
    endCard();
    ImGui::EndGroup();

    // --- Colonne de droite : le jeu choisi.
    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginChild("##srMain", ImVec2(rightW, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const Game* game = m_game.empty() ? nullptr : ctx.library.find(m_game);
    if (!game)
        drawWelcome(ctx, rightW);
    else
        drawGame(ctx, *game, rightW);
    ImGui::EndChild();
    drawPicker(ctx);
}

void SpeedrunScreen::drawWelcome(UiContext& ctx, float width)
{
    beginCard("##srWelcome", ImVec2(width, px(420.0f)), 32.0f);
    overline("BIENVENUE AUX SPEEDRUNNERS");
    text("Battez vos records, split après split", 28.0f, Weight::ExtraBold);
    space(4.0f);
    const float inner = ImGui::GetContentRegionAvail().x;
    textWrapped("Découpez votre run en étapes, lancez le jeu et appuyez sur F5 : le chrono s'affiche par-dessus le jeu, "
                "avec votre avance ou votre retard sur votre record à chaque split, et vos meilleurs segments en or.",
                14.0f, Weight::Medium, theme::Body, inner);
    space(8.0f);
    const char* points[] = {"Plusieurs catégories par jeu (Any%, 100%...) et des splits à votre goût",
                            "Record personnel, somme des meilleurs segments, nombre d'essais",
                            "Une vidéo MP4 de chaque run, dans le dossier de votre choix",
                            "L'historique complet de vos runs, terminées ou non"};
    for (const char* point : points) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + px(6.0f), p.y + px(10.0f)), px(4.0f), theme::Accent, 12);
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(20.0f), p.y));
        textWrapped(point, 14.0f, Weight::SemiBold, theme::Ink, inner - px(20.0f));
        space(2.0f);
    }
    space(10.0f);
    if (button("Choisir un jeu", {.style = ButtonStyle::Primary, .icon = draw::Icon::Plus, .height = 44.0f}))
        m_openPicker = true;
    (void)ctx;
    endCard();
}

void SpeedrunScreen::drawGame(UiContext& ctx, const Game& game, float width)
{
    speedrun::GameRuns& runs = ctx.speedrun.game(game.path);
    speedrun::Category& cat = runs.category();
    bool changed = false;

    // --- En-tête : jaquette, nom, catégories, bouton de lancement.
    beginCard("##srHead", ImVec2(width, px(262.0f)), 22.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float coverH = px(212.0f);
    const float coverW = coverH * 0.75f;
    ctx.covers.draw(dl, p, ImVec2(p.x + coverW, p.y + coverH), game, ctx.userData, px(10.0f), 12.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + coverW + px(20.0f), p.y));
    ImGui::BeginGroup();
    const float textW = width - px(44.0f) - coverW - px(20.0f);
    draw::textEllipsis(dl, ImGui::GetCursorScreenPos(), Weight::ExtraBold, 24.0f, theme::Ink, game.title, textW - px(200.0f));
    ImGui::Dummy(ImVec2(textW - px(200.0f), px(32.0f)));
    text(game.system->name, 13.0f, Weight::SemiBold, theme::Muted);
    space(6.0f);
    // Les catégories en pastilles ; la choisie est celle du chrono.
    for (size_t i = 0; i < runs.categories.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool selected = static_cast<int>(i) == runs.current;
        if (button(runs.categories[i].name.c_str(), {.style = selected ? ButtonStyle::Primary : ButtonStyle::Soft,
                                                     .height = 34.0f, .fontSize = 13.0f})) {
            runs.current = static_cast<int>(i);
            changed = true;
        }
        ImGui::PopID();
        sameLine(6.0f);
    }
    if (iconButton("##addCat", draw::Icon::Plus, ButtonStyle::Outline, 34.0f, "Nouvelle catégorie")) {
        m_categoryName[0] = '\0';
        m_openCategory = true;
    }
    space(4.0f);
    // Le record du monde de la catégorie officielle (speedrun.com).
    if (cat.worldRecord >= 0) {
        text("Record du monde : " + speedrun::formatTime(cat.worldRecord) + (cat.worldRecordBy.empty() ? "" : " par " + cat.worldRecordBy),
             13.0f, Weight::SemiBold, theme::Body);
        if (!cat.officialLink.empty()) {
            sameLine(10.0f);
            if (link("Voir le classement", 13.0f))
                SDL_OpenURL(cat.officialLink.c_str());
        }
    } else {
        text("Pas de catégorie officielle liée : cherchez-la sur speedrun.com.", 13.0f, Weight::Medium, theme::Muted);
    }
    space(4.0f);
    if (button("Importer (LiveSplit)", {.style = ButtonStyle::Soft, .icon = draw::Icon::Upload, .height = 34.0f, .fontSize = 12.0f}))
        ctx.openDialog(DialogPurpose::ImportSplits);
    sameLine(6.0f);
    if (button("Exporter .lss", {.style = ButtonStyle::Soft, .height = 34.0f, .fontSize = 12.0f}))
        ctx.openDialog(DialogPurpose::ExportSplits);
    sameLine(6.0f);
    if (button(m_official.busy() ? "Recherche…" : "Catégories officielles",
               {.style = ButtonStyle::Soft, .icon = draw::Icon::Search, .height = 34.0f, .fontSize = 12.0f,
                .disabled = m_official.busy(), .tooltip = "Les catégories et records du monde de speedrun.com"})) {
        m_officialResult.reset();
        m_official.search(game.title);
        m_openOfficial = true;
    }
    ImGui::EndGroup();
    // Lancer la run : le jeu démarre du début, chrono affiché, prêt pour F5.
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - px(44.0f) - px(190.0f), p.y));
    if (button("Lancer la run", {.style = ButtonStyle::Primary, .icon = draw::Icon::Play, .height = 44.0f, .width = 190.0f,
                                 .disabled = !ctx.library.hasCore(game)})) {
        ctx.nav.speedrunLaunch = true;
        ctx.play(game, {});
    }
    endCard();
    space(14.0f);

    // --- Les chiffres.
    {
        const ImVec2 t = ImGui::GetCursorScreenPos();
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        const float tileW = (width - px(12.0f) * 3) / 4;
        statTile(pdl, t, tileW, "RECORD PERSONNEL", speedrun::formatTime(cat.pb()), cat.pb() >= 0 ? theme::Ink : theme::Muted);
        statTile(pdl, ImVec2(t.x + (tileW + px(12.0f)), t.y), tileW, "SOMME DES MEILLEURS", speedrun::formatTime(cat.sumOfBest()),
                 cat.sumOfBest() >= 0 ? theme::Ink : theme::Muted);
        statTile(pdl, ImVec2(t.x + (tileW + px(12.0f)) * 2, t.y), tileW, "ESSAIS", std::to_string(cat.attempts), theme::Ink);
        statTile(pdl, ImVec2(t.x + (tileW + px(12.0f)) * 3, t.y), tileW, "RUNS TERMINÉES", std::to_string(cat.finished),
                 theme::Ink);
        ImGui::Dummy(ImVec2(width, px(78.0f)));
    }
    space(14.0f);

    // --- Splits (à gauche) et historique (à droite).
    const float colW = (width - px(16.0f)) / 2;
    const float splitsH = px(140.0f) + px(44.0f) * static_cast<float>(cat.segments.size()) + px(60.0f);
    const float historyH = std::max(splitsH, px(300.0f));
    beginCard("##srSplits", ImVec2(colW, std::max(splitsH, historyH)), 20.0f);
    {
        const float inner = ImGui::GetContentRegionAvail().x;
        text("Splits", 16.0f, Weight::ExtraBold);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        text("Les étapes de la run ; le dernier split arrête le chrono.", 12.0f, Weight::Medium, theme::Muted);
        space(4.0f);
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        {
            // Les titres des colonnes.
            const ImVec2 h = ImGui::GetCursorScreenPos();
            draw::text(sdl, ImVec2(h.x + px(34.0f), h.y), Weight::Bold, 10.0f, theme::Muted, "SPLIT");
            draw::text(sdl, ImVec2(h.x + inner * 0.47f, h.y), Weight::Bold, 10.0f, theme::Muted, "RECORD");
            draw::text(sdl, ImVec2(h.x + inner * 0.64f, h.y), Weight::Bold, 10.0f, theme::Muted, "MEILLEUR");
            ImGui::Dummy(ImVec2(inner, px(18.0f)));
        }
        int toDelete = -1;
        int toMoveDown = -1;
        for (size_t i = 0; i < cat.segments.size(); ++i) {
            speedrun::Segment& seg = cat.segments[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 r = ImGui::GetCursorScreenPos();
            draw::rect(sdl, r, ImVec2(r.x + inner, r.y + px(38.0f)), theme::Ground, px(10.0f));
            const std::string number = std::to_string(i + 1);
            draw::text(sdl, ImVec2(r.x + px(12.0f), r.y + px(11.0f)), Weight::Bold, 12.0f, theme::Muted, number);
            if (m_editSplit == static_cast<int>(i)) {
                // Renommer : Entrée valide.
                ImGui::SetCursorScreenPos(ImVec2(r.x + px(34.0f), r.y + px(1.0f)));
                if (textField("##name", m_splitName, sizeof(m_splitName), "Nom du split", (inner - px(90.0f)) / theme::scale()) ||
                    ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_splitName[0] != '\0') {
                        seg.name = m_splitName;
                        m_editSplit = -1;
                        changed = true;
                    }
                }
                ImGui::SetCursorScreenPos(ImVec2(r.x + inner - px(38.0f), r.y + px(3.0f)));
                if (iconButton("##ok", draw::Icon::Check, ButtonStyle::Soft, 32.0f, "Valider") && m_splitName[0] != '\0') {
                    seg.name = m_splitName;
                    m_editSplit = -1;
                    changed = true;
                }
            } else {
                draw::textEllipsis(sdl, ImVec2(r.x + px(34.0f), r.y + px(10.0f)), Weight::SemiBold, 13.0f, theme::Ink, seg.name,
                                   inner * 0.38f);
                // Le temps du record à ce split, et le meilleur segment (en or).
                const std::string pb = speedrun::formatTime(seg.pbSplit);
                const std::string best = speedrun::formatTime(seg.bestSegment);
                draw::text(sdl, ImVec2(r.x + inner * 0.47f, r.y + px(10.0f)), Weight::Bold, 13.0f, theme::Body, pb);
                draw::text(sdl, ImVec2(r.x + inner * 0.64f, r.y + px(10.0f)), Weight::Bold, 13.0f,
                           seg.bestSegment >= 0 ? theme::rgb(0xC08A00) : theme::Muted, best);
                ImGui::SetCursorScreenPos(ImVec2(r.x + inner - px(108.0f), r.y + px(3.0f)));
                if (iconButton("##edit", draw::Icon::Pencil, ButtonStyle::Ghost, 32.0f, "Renommer")) {
                    m_editSplit = static_cast<int>(i);
                    std::snprintf(m_splitName, sizeof(m_splitName), "%s", seg.name.c_str());
                }
                sameLine(4.0f);
                if (iconButton("##down", draw::Icon::ChevronDown, ButtonStyle::Ghost, 32.0f, "Descendre",
                               i + 1 >= cat.segments.size()))
                    toMoveDown = static_cast<int>(i);
                sameLine(4.0f);
                if (iconButton("##del", draw::Icon::Trash, ButtonStyle::Ghost, 32.0f, "Supprimer", cat.segments.size() <= 1))
                    toDelete = static_cast<int>(i);
            }
            ImGui::SetCursorScreenPos(r);
            ImGui::Dummy(ImVec2(inner, px(38.0f)));
            ImGui::PopID();
        }
        // (Modifier les splits change ce que les temps du record veulent dire :
        // le record et les meilleurs segments de ces splits sont effacés.)
        if (toDelete >= 0) {
            cat.segments.erase(cat.segments.begin() + toDelete);
            for (speedrun::Segment& s : cat.segments)
                s.pbSplit = -1;
            changed = true;
        }
        if (toMoveDown >= 0) {
            std::swap(cat.segments[static_cast<size_t>(toMoveDown)], cat.segments[static_cast<size_t>(toMoveDown) + 1]);
            for (speedrun::Segment& s : cat.segments) {
                s.pbSplit = -1;
                s.bestSegment = -1;
            }
            changed = true;
        }
        space(4.0f);
        textField("##newSplit", m_newSplit, sizeof(m_newSplit), "Nouveau split (ex. Monde 1)", (inner - px(110.0f)) / theme::scale());
        sameLine(8.0f);
        const bool enter = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        if ((button("Ajouter", {.style = ButtonStyle::Outline, .height = 40.0f, .width = 100.0f,
                                .disabled = m_newSplit[0] == '\0'}) ||
             enter) &&
            m_newSplit[0] != '\0') {
            // Ajouté AVANT le dernier split ("Fin" reste la fin), sauf s'il n'y a que lui.
            const auto at = cat.segments.size() <= 1 && cat.segments.front().name == "Fin" ? cat.segments.begin()
                                                                                             : cat.segments.end();
            cat.segments.insert(at, {m_newSplit});
            for (speedrun::Segment& s : cat.segments)
                s.pbSplit = -1;
            m_newSplit[0] = '\0';
            changed = true;
        }
    }
    endCard();
    ImGui::SameLine(0.0f, px(16.0f));
    beginCard("##srHistory", ImVec2(colW, std::max(splitsH, historyH)), 20.0f, theme::Surface, 24.0f, true);
    {
        const float inner = ImGui::GetContentRegionAvail().x;
        text("Historique", 16.0f, Weight::ExtraBold);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        text(std::to_string(cat.history.size()) + " runs", 12.0f, Weight::Medium, theme::Muted);
        space(4.0f);
        if (cat.history.empty())
            textWrapped("Vos runs apparaîtront ici, avec leur vidéo si l'enregistrement est activé.", 13.0f, Weight::Medium,
                        theme::Muted, inner);
        ImDrawList* hdl = ImGui::GetWindowDrawList();
        // Les plus récentes en haut.
        for (size_t n = 0; n < cat.history.size(); ++n) {
            const speedrun::Run& run = cat.history[cat.history.size() - 1 - n];
            ImGui::PushID(static_cast<int>(n));
            const ImVec2 r = ImGui::GetCursorScreenPos();
            draw::rect(hdl, r, ImVec2(r.x + inner, r.y + px(52.0f)), theme::Ground, px(10.0f));
            const bool done = run.timeMs >= 0;
            const bool isPb = done && !run.assisted && run.timeMs == cat.pb();
            const std::string time = done ? speedrun::formatTime(run.timeMs) : "—";
            draw::text(hdl, ImVec2(r.x + px(12.0f), r.y + px(8.0f)), Weight::ExtraBold, 15.0f, isPb ? theme::rgb(0xC08A00) : theme::Ink,
                       time);
            draw::text(hdl, ImVec2(r.x + px(12.0f), r.y + px(30.0f)), Weight::Medium, 11.0f, theme::Muted, dateText(run.date));
            std::string status;
            Tone tone = Tone::Neutral;
            if (run.assisted) {
                status = "Assistée";
                tone = Tone::Warning;
            } else if (isPb) {
                status = "Record";
                tone = Tone::Success;
            } else if (done) {
                status = "Terminée";
                tone = Tone::Accent;
            } else {
                status = "Abandonnée · split " + std::to_string(run.splitsDone + 1);
            }
            pillAt(hdl, ImVec2(r.x + inner * 0.38f, r.y + px(15.0f)), status.c_str(), tone, 11.0f);
            if (!run.video.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(r.x + inner - px(96.0f), r.y + px(8.0f)));
                std::error_code ec;
                const bool exists = fs::exists(fromUtf8(run.video), ec);
                if (button("Vidéo", {.style = ButtonStyle::Outline, .icon = draw::Icon::Play, .height = 36.0f, .width = 88.0f,
                                     .fontSize = 12.0f, .disabled = !exists,
                                     .tooltip = exists ? nullptr : "Fichier déplacé ou supprimé"}))
                    SDL_OpenURL(("file:///" + run.video).c_str());
            }
            ImGui::SetCursorScreenPos(r);
            ImGui::Dummy(ImVec2(inner, px(52.0f)));
            ImGui::PopID();
        }
    }
    endCard();
    space(14.0f);

    // --- Réglages : vidéo, dossier, compteur.
    Settings& s = ctx.settings;
    bool settingsChanged = false;
    beginCard("##srOptions", ImVec2(width, px(560.0f)), 22.0f);
    {
        const float inner = ImGui::GetContentRegionAvail().x;
        text("Enregistrement et réglages", 16.0f, Weight::ExtraBold);
        space(4.0f);
        auto row = [&](const char* label, const char* hint, float controlW, auto control) {
            const ImVec2 r = ImGui::GetCursorPos();
            text(label, 14.0f, Weight::Bold);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
            // (Coupé avant les boutons : un long chemin de dossier ne passe pas dessous.)
            draw::textEllipsis(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), Weight::Medium, 12.0f, theme::Muted,
                               hint, inner - controlW - px(20.0f));
            ImGui::Dummy(ImVec2(inner - controlW - px(20.0f), px(18.0f)));
            const ImVec2 after = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(r.x + inner - controlW, r.y + px(4.0f)));
            control();
            ImGui::SetCursorPos(after);
            space(6.0f);
        };
        row("Enregistrer une vidéo de chaque run", "MP4 (H.264), l'image du jeu sans le chrono, son compris", px(46.0f),
            [&] { settingsChanged |= toggle("##record", &s.speedrunRecord); });
        static const char* const keeps[] = {"Toutes", "Terminées", "Records"};
        row("Vidéos gardées", "Les runs abandonnées font vite beaucoup de fichiers",
            draw::measure(Weight::SemiBold, 13.0f, "Toutes Terminées Records").x + px(90.0f),
            [&] { settingsChanged |= segmented("##keep", keeps, &s.speedrunKeep, !s.speedrunRecord); });
        const fs::path folder = s.speedrunFolder.empty() ? ctx.dir("speedruns") : fromUtf8(s.speedrunFolder);
        row("Dossier des vidéos", toUtf8(folder).c_str(), px(130.0f * 2 + 10.0f), [&] {
            const ImVec2 b = ImGui::GetCursorScreenPos();
            if (button("Choisir…", {.style = ButtonStyle::Outline, .height = 38.0f, .width = 130.0f, .fontSize = 13.0f}))
                ctx.openDialog(DialogPurpose::ChooseSpeedrunFolder);
            ImGui::SetCursorScreenPos(ImVec2(b.x + px(140.0f), b.y));
            if (button("Ouvrir", {.style = ButtonStyle::Outline, .height = 38.0f, .width = 130.0f, .fontSize = 13.0f}))
                openFolder(folder);
        });
        row("Le chrono dans la vidéo", "La vidéo garde ce qui est affiché par-dessus le jeu (chrono, touches)", px(46.0f),
            [&] { settingsChanged |= toggle("##overlayVideo", &s.speedrunVideoOverlay, !s.speedrunRecord); });
        row("Afficher les touches (input display)", "Une manette à l'écran montre les boutons appuyés, dans tous les jeux",
            px(46.0f), [&] { settingsChanged |= toggle("##inputDisplay", &s.inputDisplay); });
        // Les boutons de la manette qui commandent le chrono (aucun par défaut
        // pour la remise à zéro : un appui malencontreux coûterait la run).
        static const char* const padNames[] = {"Aucun", "Clic stick gauche (L3)", "Clic stick droit (R3)", "Bouton central",
                                               "Bouton Partage / Capture", "Pavé tactile"};
        static const int padButtons[] = {-1, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_GUIDE,
                                         SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_TOUCHPAD};
        auto padRow = [&](const char* label, const char* id, int* value) {
            int current = 0;
            for (int i = 0; i < 6; ++i)
                if (padButtons[i] == *value)
                    current = i;
            row(label, "Bouton de la manette, pendant le jeu", px(240.0f), [&] {
                if (combo(id, padNames, &current, 240.0f)) {
                    *value = padButtons[current];
                    settingsChanged = true;
                }
            });
        };
        padRow("Manette : départ et split", "##padSplit", &s.speedrunPadSplit);
        padRow("Manette : remise à zéro", "##padReset", &s.speedrunPadReset);
        row("Remettre le jeu à zéro avec le chrono", "F9 relance aussi le jeu depuis le début : chaque essai part du même point",
            px(46.0f), [&] { settingsChanged |= toggle("##resetGame", &s.speedrunResetGame); });
        // Le compteur d'essais à zéro (le record et l'historique restent) : deux clics.
        row("Remettre le compteur à zéro", "Les essais et les runs terminées de cette catégorie repartent de 0", px(150.0f), [&] {
            if (button(m_confirmCounter ? "Confirmer ?" : "Remettre à 0",
                       {.style = m_confirmCounter ? ButtonStyle::Danger : ButtonStyle::Outline, .height = 38.0f,
                        .width = 150.0f, .fontSize = 13.0f})) {
                if (m_confirmCounter) {
                    cat.attempts = 0;
                    cat.finished = 0;
                    changed = true;
                    ctx.notify("Compteur remis à zéro");
                }
                m_confirmCounter = !m_confirmCounter;
            }
        });
    }
    endCard();
    if (settingsChanged)
        ctx.settingsChanged();
    if (changed)
        ctx.speedrun.save();
    space(20.0f);

    drawCategoryPopup(ctx, runs);
    drawOfficialPopup(ctx, runs, game);
}

void SpeedrunScreen::importSplits(UiContext& ctx, const fs::path& file)
{
    const Game* game = m_game.empty() ? nullptr : ctx.library.find(m_game);
    if (!game) {
        ctx.notify("Choisissez d'abord un jeu");
        return;
    }
    speedrun::Category imported;
    std::string gameName;
    std::string error;
    if (!speedrun::importLss(file, imported, gameName, error)) {
        ctx.notify(error);
        return;
    }
    // Une catégorie du même nom est remplacée (ses runs déjà faites ici restent).
    speedrun::GameRuns& runs = ctx.speedrun.game(game->path);
    auto same = std::find_if(runs.categories.begin(), runs.categories.end(),
                             [&](const speedrun::Category& c) { return c.name == imported.name; });
    if (same != runs.categories.end()) {
        imported.history = std::move(same->history);
        imported.finished = same->finished;
        imported.worldRecord = same->worldRecord;
        imported.worldRecordBy = same->worldRecordBy;
        imported.officialLink = same->officialLink;
        *same = std::move(imported);
        runs.current = static_cast<int>(same - runs.categories.begin());
    } else {
        runs.categories.push_back(std::move(imported));
        runs.current = static_cast<int>(runs.categories.size()) - 1;
    }
    ctx.speedrun.save();
    const speedrun::Category& cat = runs.category();
    ctx.notify(std::to_string(cat.segments.size()) + " splits importés : " + cat.name);
}

void SpeedrunScreen::exportSplits(UiContext& ctx, const fs::path& file)
{
    const Game* game = m_game.empty() ? nullptr : ctx.library.find(m_game);
    if (!game)
        return;
    fs::path out = file;
    if (out.extension().empty())
        out += ".lss";
    const speedrun::Category& cat = ctx.speedrun.game(game->path).category();
    ctx.notify(speedrun::exportLss(out, cat, game->title) ? "Splits exportés : " + toUtf8(out.filename())
                                                          : "Impossible d'écrire " + toUtf8(out.filename()));
}

void SpeedrunScreen::drawOfficialPopup(UiContext& ctx, speedrun::GameRuns& runs, const Game& game)
{
    if (m_openOfficial) {
        ImGui::OpenPopup("##srOfficial");
        m_openOfficial = false;
    }
    if (!m_officialResult)
        m_officialResult = m_official.take();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(620.0f), std::min(px(560.0f), viewport->Size.y - px(32.0f))));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(28.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##srOfficial", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 top = ImGui::GetCursorPos();
        text("Catégories officielles", 22.0f, Weight::ExtraBold);
        text(m_officialResult && !m_officialResult->gameName.empty() ? m_officialResult->gameName + " · speedrun.com"
                                                                      : game.title + " · speedrun.com",
             13.0f, Weight::Medium, theme::Muted);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(top.x + width - px(40.0f), top.y));
        if (iconButton("##close", draw::Icon::Close, ButtonStyle::Soft, 40.0f))
            ImGui::CloseCurrentPopup();
        ImGui::SetCursorPos(after);
        space(6.0f);
        if (!m_officialResult) {
            text("Recherche sur speedrun.com…", 14.0f, Weight::SemiBold, theme::Body);
        } else if (!m_officialResult->error.empty()) {
            textWrapped(m_officialResult->error, 14.0f, Weight::SemiBold, theme::Warning, width);
            space(4.0f);
            textWrapped("Le nom du jeu doit être proche de celui de speedrun.com (renommez le jeu dans sa fiche au besoin).", 12.0f,
                        Weight::Medium, theme::Muted, width);
        } else {
            ImGui::BeginChild("##cats", ImVec2(0, 0));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rowW = ImGui::GetContentRegionAvail().x;
            for (size_t i = 0; i < m_officialResult->categories.size(); ++i) {
                const SpeedrunCom::OfficialCategory& oc = m_officialResult->categories[i];
                ImGui::PushID(static_cast<int>(i));
                const ImVec2 r = ImGui::GetCursorScreenPos();
                draw::rect(dl, r, ImVec2(r.x + rowW, r.y + px(58.0f)), theme::Ground, px(12.0f));
                draw::textEllipsis(dl, ImVec2(r.x + px(14.0f), r.y + px(10.0f)), Weight::Bold, 14.0f, theme::Ink, oc.name,
                                   rowW - px(170.0f));
                const std::string wr = oc.wrMs >= 0 ? "WR " + speedrun::formatTime(oc.wrMs) + (oc.wrBy.empty() ? "" : " · " + oc.wrBy)
                                                    : std::string("Pas encore de record");
                draw::textEllipsis(dl, ImVec2(r.x + px(14.0f), r.y + px(32.0f)), Weight::Medium, 12.0f, theme::Muted, wr,
                                   rowW - px(170.0f));
                auto existing = std::find_if(runs.categories.begin(), runs.categories.end(),
                                             [&](const speedrun::Category& c) { return c.name == oc.name; });
                ImGui::SetCursorScreenPos(ImVec2(r.x + rowW - px(140.0f), r.y + px(11.0f)));
                if (button(existing != runs.categories.end() ? "Mettre à jour" : "Ajouter",
                           {.style = ButtonStyle::Outline, .height = 36.0f, .width = 126.0f, .fontSize = 12.0f})) {
                    speedrun::Category* target = nullptr;
                    if (existing != runs.categories.end()) {
                        target = &*existing;
                    } else {
                        // (Le "Any%" vide créé d'office est remplacé plutôt que gardé à côté.)
                        speedrun::Category c;
                        c.name = oc.name;
                        c.segments.push_back({"Fin"});
                        runs.categories.push_back(std::move(c));
                        target = &runs.categories.back();
                    }
                    target->worldRecord = oc.wrMs;
                    target->worldRecordBy = oc.wrBy;
                    target->officialLink = oc.weblink;
                    runs.current = static_cast<int>(target - runs.categories.data());
                    ctx.speedrun.save();
                    ctx.notify("Catégorie officielle : " + oc.name);
                }
                ImGui::SetCursorScreenPos(r);
                ImGui::Dummy(ImVec2(rowW, px(58.0f)));
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void SpeedrunScreen::drawCategoryPopup(UiContext& ctx, speedrun::GameRuns& runs)
{
    if (m_openCategory) {
        ImGui::OpenPopup("##srCategory");
        m_openCategory = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(440.0f), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(26.0f, 26.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##srCategory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float inner = px(388.0f);
        text("Nouvelle catégorie", 20.0f, Weight::ExtraBold);
        text("Any%, 100%, Glitchless, 120 étoiles...", 13.0f, Weight::Medium, theme::Muted);
        space(6.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        textField("##catName", m_categoryName, sizeof(m_categoryName), "Nom de la catégorie", inner / theme::scale());
        space(8.0f);
        const bool valid = m_categoryName[0] != '\0';
        if (button("Créer", {.style = ButtonStyle::Primary, .height = 40.0f, .width = 120.0f, .disabled = !valid}) ||
            (valid && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            speedrun::Category c;
            c.name = m_categoryName;
            c.segments.push_back({"Fin"});
            runs.categories.push_back(std::move(c));
            runs.current = static_cast<int>(runs.categories.size()) - 1;
            ctx.speedrun.save();
            ImGui::CloseCurrentPopup();
        }
        sameLine(8.0f);
        if (button("Annuler", {.style = ButtonStyle::Ghost, .height = 40.0f}))
            ImGui::CloseCurrentPopup();
        // La catégorie choisie peut aussi être supprimée d'ici (s'il en reste une autre).
        if (runs.categories.size() > 1) {
            space(10.0f);
            const std::string label = "Supprimer « " + runs.category().name + " »";
            if (link(label.c_str(), 13.0f)) {
                runs.categories.erase(runs.categories.begin() + runs.current);
                runs.current = 0;
                ctx.speedrun.save();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void SpeedrunScreen::drawPicker(UiContext& ctx)
{
    if (m_openPicker) {
        ImGui::OpenPopup("##srPicker");
        m_openPicker = false;
        m_search[0] = '\0';
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(620.0f), std::min(px(620.0f), viewport->Size.y - px(32.0f))));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(28.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##srPicker", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 top = ImGui::GetCursorPos();
        text("Choisir un jeu", 22.0f, Weight::ExtraBold);
        text("Le jeu de vos runs (les émulateurs externes ne sont pas pris en charge).", 13.0f, Weight::Medium, theme::Muted);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(top.x + width - px(40.0f), top.y));
        if (iconButton("##close", draw::Icon::Close, ButtonStyle::Soft, 40.0f))
            ImGui::CloseCurrentPopup();
        ImGui::SetCursorPos(after);
        space(4.0f);
        searchField("##search", m_search, sizeof(m_search), "Rechercher", width / theme::scale(), ImGui::IsWindowAppearing());
        space(4.0f);
        ImGui::BeginChild("##list", ImVec2(0, 0));
        const std::string needle = lower(m_search);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowW = ImGui::GetContentRegionAvail().x;
        for (const Game& g : ctx.library.games()) {
            if (g.system->external() || (!needle.empty() && lower(g.title).find(needle) == std::string::npos))
                continue;
            ImGui::PushID(&g);
            const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, px(64.0f)), ImGuiButtonFlags_EnableNav);
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 p = ImGui::GetItemRectMin();
            if (hovered)
                draw::rect(dl, p, ImVec2(p.x + rowW, p.y + px(64.0f)), theme::Ground, px(12.0f));
            ctx.covers.draw(dl, ImVec2(p.x + px(8.0f), p.y + px(6.0f)), ImVec2(p.x + px(47.0f), p.y + px(58.0f)), g, ctx.userData,
                            px(6.0f), 0.0f);
            draw::textEllipsis(dl, ImVec2(p.x + px(60.0f), p.y + px(13.0f)), Weight::Bold, 14.0f, theme::Ink, g.title,
                               rowW - px(80.0f));
            draw::text(dl, ImVec2(p.x + px(60.0f), p.y + px(34.0f)), Weight::Medium, 12.0f, theme::Muted, g.system->name);
            if (clicked) {
                m_game = g.path;
                ctx.speedrun.game(g.path); // (créé avec une catégorie "Any%")
                ctx.speedrun.save();
                ImGui::CloseCurrentPopup();
            }
        }
        if (ctx.library.games().empty())
            text("Votre bibliothèque est vide : ajoutez d'abord des jeux.", 14.0f, Weight::Medium, theme::Muted);
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

} // namespace ui
