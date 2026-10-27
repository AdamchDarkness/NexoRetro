// Big Picture : l'interface pour jouer depuis le canapé, sur la télé.
//
// Plein écran, sur fond sombre, avec de grandes jaquettes, et entièrement
// pilotable à la manette :
//   croix / stick gauche / flèches   jeu précédent, suivant
//   LB / RB (Page préc. / suiv.)     catégorie précédente, suivante
//   A (Entrée)                       jouer
//   X (R)                            reprendre la partie (sauvegarde automatique)
//   Y (F)                            ajouter aux favoris, ou retirer
//   B, Start (Échap)                 quitter Big Picture
// La souris marche aussi : clic sur une jaquette, molette.
//
// La fenêtre ImGui n'a pas de "navigation" (NoNav) : c'est cet écran qui
// lit lui-même les boutons de la manette.

#include "Screens.hpp"
#include "Widgets.hpp"
#include "../Compat.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ctime>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

// Les couleurs de l'écran sombre.
const ImU32 kNight = theme::rgb(0x0B0C10);
const ImU32 kWhite = theme::rgb(0xFFFFFF);
const ImU32 kDim = theme::withAlpha(theme::rgb(0xFFFFFF), 150);

// repeat : se répète si on garde le bouton enfoncé (pour défiler) ; les
// actions (jouer, quitter...) ne partent qu'une fois par appui.
bool pressed(std::initializer_list<ImGuiKey> keys, bool repeat = false)
{
    for (const ImGuiKey k : keys)
        if (ImGui::IsKeyPressed(k, repeat))
            return true;
    return false;
}

// Un bouton de manette dessiné façon Xbox : rond de couleur et sa lettre,
// ou pilule "LB / RB". Renvoie la largeur occupée.
float buttonGlyph(ImDrawList* dl, ImVec2 pos, const char* name, float height)
{
    const std::string label(name);
    if (label.size() == 1) {
        ImU32 color = theme::rgb(0x3BA55D); // A : vert
        if (label == "B") color = theme::rgb(0xE5484D);
        if (label == "X") color = theme::rgb(0x2F7BF5);
        if (label == "Y") color = theme::rgb(0xF5B800);
        const float r = height / 2;
        dl->AddCircleFilled(ImVec2(pos.x + r, pos.y + r), r, color);
        const ImVec2 ts = draw::measure(Weight::ExtraBold, 14.0f, label);
        draw::text(dl, ImVec2(pos.x + r - ts.x / 2, pos.y + r - ts.y / 2), Weight::ExtraBold, 14.0f, kWhite, label);
        return height;
    }
    const ImVec2 ts = draw::measure(Weight::ExtraBold, 12.0f, label);
    const float w = ts.x + px(16.0f);
    draw::rect(dl, pos, ImVec2(pos.x + w, pos.y + height), theme::withAlpha(kWhite, 40), height / 2,
               theme::withAlpha(kWhite, 90));
    draw::text(dl, ImVec2(pos.x + px(8.0f), pos.y + (height - ts.y) / 2), Weight::ExtraBold, 12.0f, kWhite, label);
    return w;
}

} // namespace

void BigPictureScreen::draw(UiContext& ctx)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kNight);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##bigpicture", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = vp->Size.x;
    const float H = vp->Size.y;
    const ImVec2 o = vp->Pos;
    const float margin = std::max(px(48.0f), W * 0.05f);

    // --- Les catégories : Tous, Favoris, Récents, puis une par console.
    struct Category {
        std::string id;
        std::string label;
    };
    std::vector<Category> categories = {{"all", "Tous"}, {"fav", "Favoris"}, {"recent", "Récents"}};
    for (const System& s : Library::systems())
        if (ctx.library.countFor(s) > 0)
            categories.push_back({s.id, s.shortName});
    m_category = std::clamp(m_category, 0, static_cast<int>(categories.size()) - 1);
    const std::string& categoryId = categories[static_cast<size_t>(m_category)].id;

    std::vector<const Game*> games;
    for (const Game& g : ctx.library.games()) {
        const GameRecord* r = ctx.userData.find(g.path);
        const bool in = categoryId == "all" || (categoryId == "fav" && r && r->favorite) ||
                        (categoryId == "recent" && r && r->lastPlayed > 0) || categoryId == g.system->id;
        if (in)
            games.push_back(&g);
    }
    if (categoryId == "recent")
        std::stable_sort(games.begin(), games.end(), [&](const Game* a, const Game* b) {
            const GameRecord* ra = ctx.userData.find(a->path);
            const GameRecord* rb = ctx.userData.find(b->path);
            return (ra ? ra->lastPlayed : 0) > (rb ? rb->lastPlayed : 0);
        });
    const int count = static_cast<int>(games.size());

    // --- Les commandes (manette, clavier, souris).
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!popup) {
        if (pressed({ImGuiKey_GamepadL1, ImGuiKey_PageUp})) {
            m_category = (m_category + static_cast<int>(categories.size()) - 1) % static_cast<int>(categories.size());
            m_index = 0;
            m_scroll = 0.0f;
        }
        if (pressed({ImGuiKey_GamepadR1, ImGuiKey_PageDown})) {
            m_category = (m_category + 1) % static_cast<int>(categories.size());
            m_index = 0;
            m_scroll = 0.0f;
        }
        if (pressed({ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow}, true))
            --m_index;
        if (pressed({ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow}, true))
            ++m_index;
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            m_index -= ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1;
    }
    m_index = std::clamp(m_index, 0, std::max(0, count - 1));
    // La rangée glisse doucement jusqu'au jeu choisi.
    m_scroll += (static_cast<float>(m_index) - m_scroll) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);
    if (std::abs(m_scroll - m_index) > 50.0f)
        m_scroll = static_cast<float>(m_index);
    const Game* selected = count > 0 ? games[static_cast<size_t>(m_index)] : nullptr;
    const scrape::GameInfo* info = selected ? ctx.scraper.find(selected->path) : nullptr;

    // --- Le fond : la capture d'écran du jeu choisi, très assombrie.
    if (info && !info->snap.empty())
        ctx.covers.drawImage(dl, o, ImVec2(o.x + W, o.y + H), info->snap, 0.0f);
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), theme::withAlpha(kNight, 200));
    dl->AddRectFilledMultiColor(ImVec2(o.x, o.y + H * 0.45f), ImVec2(o.x + W, o.y + H), theme::withAlpha(kNight, 0),
                                theme::withAlpha(kNight, 0), kNight, kNight);

    // --- En haut : le nom, les catégories (LB / RB) et l'heure.
    const float topY = o.y + px(36.0f);
    float x = o.x + margin + brand(ctx, dl, ImVec2(o.x + margin, topY), 30.0f, 18.0f, kWhite) + px(40.0f);
    x += buttonGlyph(dl, ImVec2(x, topY + px(3.0f)), "LB", px(24.0f)) + px(12.0f);
    for (size_t i = 0; i < categories.size(); ++i) {
        const bool active = static_cast<int>(i) == m_category;
        const std::string& label = categories[i].label;
        const ImVec2 ts = draw::measure(Weight::Bold, 16.0f, label);
        const ImVec2 min(x, topY - px(2.0f));
        const ImVec2 max(x + ts.x + px(28.0f), topY + px(32.0f));
        if (max.x > o.x + W - margin - px(160.0f))
            break; // (trop de consoles : on garde la place de l'heure)
        if (active)
            draw::rect(dl, min, max, kWhite, (max.y - min.y) / 2);
        draw::text(dl, ImVec2(x + px(14.0f), topY + px(15.0f) - ts.y / 2), Weight::Bold, 16.0f, active ? kNight : kDim, label);
        // Un clic sur une catégorie la choisit.
        if (ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_category = static_cast<int>(i);
            m_index = 0;
            m_scroll = 0.0f;
        }
        x = max.x + px(6.0f);
    }
    buttonGlyph(dl, ImVec2(x + px(6.0f), topY + px(3.0f)), "RB", px(24.0f));

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char clock[8];
    std::strftime(clock, sizeof(clock), "%H:%M", &local);
    const ImVec2 cs = draw::measure(Weight::Bold, 22.0f, clock);
    draw::text(dl, ImVec2(o.x + W - margin - cs.x, topY + px(1.0f)), Weight::Bold, 22.0f, kWhite, clock);

    // --- La rangée de jaquettes : le jeu choisi à gauche, plus grand et encadré.
    const float coverH = H * 0.40f;
    const float coverW = coverH * 0.75f;
    const float bigScale = 1.16f;
    const float gap = px(26.0f);
    const float rowCenterY = o.y + H * 0.44f;
    int clicked = -1;
    if (count == 0) {
        const char* empty = categoryId == "fav" ? "Aucun favori pour l'instant" : "Aucun jeu dans cette catégorie";
        const ImVec2 es = draw::measure(Weight::ExtraBold, 26.0f, empty);
        draw::text(dl, ImVec2(o.x + (W - es.x) / 2, rowCenterY - es.y), Weight::ExtraBold, 26.0f, kWhite, empty);
        const char* hint = categoryId == "fav" ? "Appuyez sur Y sur un jeu pour l'ajouter aux favoris."
                                               : "Ajoutez des jeux depuis l'interface habituelle.";
        const ImVec2 hs = draw::measure(Weight::Medium, 16.0f, hint);
        draw::text(dl, ImVec2(o.x + (W - hs.x) / 2, rowCenterY + px(10.0f)), Weight::Medium, 16.0f, kDim, hint);
    }
    for (int i = std::max(0, static_cast<int>(m_scroll) - 2); i < count; ++i) {
        const float offset = static_cast<float>(i) - m_scroll;
        // Le jeu choisi grandit ; les suivants sont décalés d'autant.
        const float focus = std::clamp(1.0f - std::abs(offset), 0.0f, 1.0f);
        const float scale = 1.0f + (bigScale - 1.0f) * focus;
        const float w = coverW * scale;
        const float h = coverH * scale;
        float left = o.x + margin + offset * (coverW + gap);
        if (offset > 0.0f)
            left += coverW * (bigScale - 1.0f) * std::min(offset, 1.0f);
        if (left > o.x + W)
            break;
        if (left + w < o.x)
            continue;
        const ImVec2 min(left, rowCenterY - h / 2);
        const ImVec2 max(left + w, rowCenterY + h / 2);
        draw::shadow(dl, min, max, px(18.0f), px(22.0f), px(10.0f), 90);
        ctx.covers.draw(dl, min, max, *games[static_cast<size_t>(i)], ctx.userData, px(18.0f), 22.0f * scale);
        if (i == m_index)
            dl->AddRect(ImVec2(min.x - px(5.0f), min.y - px(5.0f)), ImVec2(max.x + px(5.0f), max.y + px(5.0f)), kWhite,
                        px(22.0f), 0, px(4.0f));
        else
            draw::rect(dl, min, max, theme::withAlpha(kNight, 110), px(18.0f));
        if (ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            clicked = i;
    }

    // --- Sous la rangée : le jeu choisi.
    std::filesystem::path resumeFile;
    if (selected) {
        const GameRecord* r = ctx.userData.find(selected->path);
        float y = rowCenterY + coverH * bigScale / 2 + px(40.0f);
        const float textX = o.x + margin;
        draw::textEllipsis(dl, ImVec2(textX, y), Weight::ExtraBold, 40.0f, kWhite, selected->title, W - margin * 2 - px(60.0f));
        if (r && r->favorite) {
            const float tw = std::min(draw::measure(Weight::ExtraBold, 40.0f, selected->title).x, W - margin * 2 - px(60.0f));
            draw::icon(dl, draw::Icon::Heart, ImVec2(textX + tw + px(16.0f), y + px(10.0f)), px(28.0f), theme::Favorite);
        }
        y += px(58.0f);
        std::string details = selected->system->name;
        if (info && info->year)
            details += "  ·  " + std::to_string(info->year);
        if (info && !info->genre.empty())
            details += "  ·  " + info->genre;
        draw::text(dl, ImVec2(textX, y), Weight::SemiBold, 20.0f, kDim, details);
        y += px(34.0f);
        // Temps de jeu et dernière partie (chaque morceau est traduit à part).
        float lx = textX;
        auto piece = [&](const std::string& t) {
            draw::text(dl, ImVec2(lx, y), Weight::Medium, 16.0f, kDim, t);
            lx += draw::measure(Weight::Medium, 16.0f, t).x;
        };
        if (r && r->playSeconds > 0) {
            piece(formatDuration(r->playSeconds) + " de jeu");
            piece("  ·  ");
        }
        piece(formatAgo(r ? r->lastPlayed : 0));
        // Une sauvegarde automatique : on peut reprendre exactement là où on était.
        const std::filesystem::path state = autoStateFile(ctx, *selected);
        if (!selected->system->external() && fileTime(state) > 0)
            resumeFile = state;
    }

    // --- En bas : les boutons de la manette et ce qu'ils font.
    {
        const float barY = o.y + H - px(64.0f);
        const float glyphH = px(30.0f);
        struct Hint {
            const char* button;
            const char* label;
        };
        std::vector<Hint> hints;
        if (selected) {
            hints.push_back({"A", "Jouer"});
            if (!resumeFile.empty())
                hints.push_back({"X", "Reprendre"});
            const GameRecord* r = ctx.userData.find(selected->path);
            hints.push_back({"Y", r && r->favorite ? "Retirer des favoris" : "Ajouter aux favoris"});
        }
        hints.push_back({"B", "Quitter Big Picture"});
        float total = 0.0f;
        for (const Hint& h : hints)
            total += glyphH + px(10.0f) + draw::measure(Weight::Bold, 16.0f, h.label).x + px(32.0f);
        float hx = o.x + W - margin - total + px(32.0f);
        for (const Hint& h : hints) {
            hx += buttonGlyph(dl, ImVec2(hx, barY), h.button, glyphH) + px(10.0f);
            const ImVec2 ls = draw::measure(Weight::Bold, 16.0f, h.label);
            draw::text(dl, ImVec2(hx, barY + (glyphH - ls.y) / 2), Weight::Bold, 16.0f, kWhite, h.label);
            hx += ls.x + px(32.0f);
        }
        const std::string position =
            count > 0 ? std::to_string(m_index + 1) + " / " + std::to_string(count) : std::string();
        draw::text(dl, ImVec2(o.x + margin, barY + px(5.0f)), Weight::SemiBold, 16.0f, kDim, position);
    }

    // --- Les actions (après le dessin : jouer change d'écran).
    if (clicked >= 0) {
        if (clicked == m_index && selected)
            ctx.play(*selected, {});
        else
            m_index = clicked;
    } else if (!popup && selected && pressed({ImGuiKey_GamepadFaceDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter})) {
        ctx.play(*selected, {}); // (l'émulateur manquant se télécharge d'abord, voir App)
    } else if (!popup && selected && !resumeFile.empty() && pressed({ImGuiKey_GamepadFaceLeft, ImGuiKey_R})) {
        ctx.play(*selected, resumeFile);
    } else if (!popup && selected && pressed({ImGuiKey_GamepadFaceUp, ImGuiKey_F})) {
        GameRecord& r = ctx.userData.record(selected->path);
        r.favorite = !r.favorite;
        ctx.userData.save();
    } else if (!popup && pressed({ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadStart, ImGuiKey_Escape, ImGuiKey_AppBack})) {
        ctx.setBigPicture(false);
    }

    ImGui::End();
}

} // namespace ui
