#include "AchievementPopup.hpp"

#include "Covers.hpp"
#include "Draw.hpp"
#include "Theme.hpp"
#include "Widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

using theme::px;
using theme::Weight;

namespace ui {

namespace {
constexpr Uint64 kShowMs = 5000;  // durée d'affichage d'un bandeau
constexpr float kFadeMs = 250.0f; // apparition et disparition en fondu
const ImU32 kGold = IM_COL32(0xE8, 0xA3, 0x17, 255);
} // namespace

void AchievementPopups::push(Achievements::Notice notice)
{
    // (Au plus quelques messages en attente : une avalanche de succès d'un
    // coup ne doit pas occuper l'écran pendant une minute.)
    if (m_queue.size() < 6)
        m_queue.push_back(std::move(notice));
}

void AchievementPopups::draw(const Covers& covers)
{
    if (m_queue.empty())
        return;
    const Uint64 now = SDL_GetTicks();
    if (m_shownAt == 0)
        m_shownAt = now;
    if (now - m_shownAt > kShowMs) {
        m_queue.pop_front();
        m_shownAt = m_queue.empty() ? 0 : now;
        if (m_queue.empty())
            return;
    }
    using Kind = Achievements::Notice::Kind;
    const Achievements::Notice& n = m_queue.front();
    const float age = static_cast<float>(now - m_shownAt);
    const float fade = std::clamp(age / kFadeMs, 0.0f, 1.0f) * std::clamp((kShowMs - age) / kFadeMs, 0.0f, 1.0f);
    const int alpha = static_cast<int>(255 * fade);

    // Une carte au centre, en haut, qui descend un peu en apparaissant.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(px(460.0f), viewport->Size.x - px(32.0f));
    const float height = px(92.0f);
    const ImVec2 min(viewport->Pos.x + (viewport->Size.x - width) / 2,
                     viewport->Pos.y + px(24.0f) - px(12.0f) * (1.0f - std::clamp(age / kFadeMs, 0.0f, 1.0f)));
    const ImVec2 max(min.x + width, min.y + height);
    const ImU32 accent = n.kind == Kind::Unlocked || n.kind == Kind::Mastered ? kGold
                         : n.kind == Kind::Error                              ? theme::Danger
                                                                              : theme::Accent;
    draw::shadow(dl, min, max, px(20.0f), px(18.0f), px(10.0f), static_cast<int>(60 * fade));
    draw::rect(dl, min, max, theme::withAlpha(theme::Surface, alpha), px(20.0f), theme::withAlpha(accent, alpha));

    // L'image du succès (ou une pastille, le temps qu'elle arrive).
    const float pad = px(14.0f);
    const ImVec2 badgeMin(min.x + pad, min.y + pad);
    const ImVec2 badgeMax(badgeMin.x + height - 2 * pad, badgeMin.y + height - 2 * pad);
    if (n.badge.empty() || fade < 0.99f || !covers.drawImage(dl, badgeMin, badgeMax, n.badge, px(12.0f), 128)) {
        draw::rect(dl, badgeMin, badgeMax, theme::withAlpha(accent, alpha / 5), px(12.0f));
        const float s = px(28.0f);
        const ImVec2 c((badgeMin.x + badgeMax.x) / 2, (badgeMin.y + badgeMax.y) / 2);
        draw::icon(dl, n.kind == Kind::Info ? draw::Icon::Gamepad : draw::Icon::Check, ImVec2(c.x - s / 2, c.y - s / 2), s,
                   theme::withAlpha(accent, alpha));
    }

    // Le texte : la ligne du haut dit ce qui arrive, puis le nom et la description.
    const float textX = badgeMax.x + px(14.0f);
    const float textW = max.x - textX - pad;
    std::string head;
    switch (n.kind) {
    case Kind::Unlocked: head = "Succès débloqué"; break;
    case Kind::Mastered: head = "Bravo !"; break;
    case Kind::Error: head = "RetroAchievements"; break;
    case Kind::Info: head = "RetroAchievements"; break;
    }
    draw::textEllipsis(dl, ImVec2(textX, min.y + px(13.0f)), Weight::ExtraBold, 11.0f, theme::withAlpha(accent, alpha), head,
                       textW - px(70.0f));
    if (n.points > 0) {
        const std::string points = std::to_string(n.points) + " pts";
        const ImVec2 ps = draw::measure(Weight::ExtraBold, 11.0f, points);
        draw::text(dl, ImVec2(max.x - pad - ps.x, min.y + px(13.0f)), Weight::ExtraBold, 11.0f, theme::withAlpha(accent, alpha),
                   points);
    }
    const std::string& title = n.kind == Kind::Error ? n.text : n.title;
    draw::textEllipsis(dl, ImVec2(textX, min.y + px(31.0f)), Weight::ExtraBold, 15.0f, theme::withAlpha(theme::Ink, alpha), title,
                       textW);
    if (n.kind != Kind::Error)
        draw::textEllipsis(dl, ImVec2(textX, min.y + px(55.0f)), Weight::Medium, 12.0f, theme::withAlpha(theme::Muted, alpha),
                           n.text, textW);
}

bool drawAchievementList(const Achievements& achievements, const Covers& covers, const std::string& gameTitle)
{
    bool open = true;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), IM_COL32(0, 0, 0, 150));
    const ImVec2 size(std::min(px(760.0f), viewport->Size.x - px(32.0f)), std::min(px(640.0f), viewport->Size.y - px(32.0f)));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(26.0f, 26.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(24.0f));
    ImGui::Begin("##achievements", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float inner = ImGui::GetContentRegionAvail().x;
    const ImVec2 top = ImGui::GetCursorPos();
    const Achievements::GameSummary s = achievements.summary();
    text("Succès", 22.0f, Weight::ExtraBold);
    text(s.title.empty() ? gameTitle : s.title, 13.0f, Weight::SemiBold, theme::Muted);
    const ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(top.x + inner - px(120.0f), top.y));
    if (button("Retour", {.style = ButtonStyle::Soft, .icon = draw::Icon::ChevronLeft, .height = 40.0f, .width = 120.0f}) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        open = false;
    ImGui::SetCursorPos(after);
    space(6.0f);

    // L'avancement : succès et points débloqués, et une barre.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string count = std::to_string(s.unlocked) + " / " + std::to_string(s.total) + " succès · " +
                              std::to_string(s.pointsUnlocked) + " / " + std::to_string(s.points) + " points" +
                              (achievements.hardcore() ? " · Hardcore" : "");
    text(count, 14.0f, Weight::Bold);
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ratio = s.total > 0 ? static_cast<float>(s.unlocked) / s.total : 0.0f;
        draw::rect(dl, p, ImVec2(p.x + inner, p.y + px(8.0f)), theme::Track, px(4.0f));
        if (ratio > 0)
            draw::rect(dl, p, ImVec2(p.x + inner * ratio, p.y + px(8.0f)), kGold, px(4.0f));
        ImGui::Dummy(ImVec2(inner, px(8.0f)));
    }
    space(8.0f);

    // La liste, qui défile : à faire d'abord, puis ceux déjà obtenus.
    ImGui::BeginChild("##list", ImVec2(inner, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_NavFlattened);
    const float rowW = ImGui::GetContentRegionAvail().x;
    const float rowH = px(72.0f);
    int index = 0;
    for (const Achievements::Item& a : achievements.items()) {
        ImGui::PushID(index++);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* ldl = ImGui::GetWindowDrawList();
        draw::rect(ldl, p, ImVec2(p.x + rowW, p.y + rowH - px(6.0f)), a.unlocked ? theme::withAlpha(kGold, 28) : theme::Ground,
                   px(14.0f));
        const ImVec2 bMin(p.x + px(8.0f), p.y + px(6.0f));
        const ImVec2 bMax(bMin.x + rowH - px(18.0f), bMin.y + rowH - px(18.0f));
        if (a.badge.empty() || !covers.drawImage(ldl, bMin, bMax, a.badge, px(10.0f), 128))
            draw::rect(ldl, bMin, bMax, theme::Placeholder, px(10.0f));
        const float tx = bMax.x + px(14.0f);
        const std::string points = std::to_string(a.points) + " pts";
        const float pw = draw::measure(Weight::ExtraBold, 12.0f, points).x;
        draw::textEllipsis(ldl, ImVec2(tx, p.y + px(10.0f)), Weight::ExtraBold, 14.0f, a.unlocked ? theme::Ink : theme::Body,
                           a.title, rowW - (tx - p.x) - pw - px(24.0f));
        draw::text(ldl, ImVec2(p.x + rowW - pw - px(14.0f), p.y + px(12.0f)), Weight::ExtraBold, 12.0f,
                   a.unlocked ? kGold : theme::Muted, points);
        std::string line = a.unsupported ? "Pas encore pris en charge par cette version" : a.description;
        if (!a.unlocked && !a.progress.empty())
            line = a.progress + " · " + line;
        draw::textEllipsis(ldl, ImVec2(tx, p.y + px(32.0f)), Weight::Medium, 12.0f, theme::Muted, line,
                           rowW - (tx - p.x) - px(14.0f));
        // Un succès mesuré ("12/50") : une fine barre d'avancement.
        if (!a.unlocked && a.percent > 0.0f) {
            const float bw = rowW - (tx - p.x) - px(14.0f);
            const ImVec2 b(tx, p.y + px(52.0f));
            draw::rect(ldl, b, ImVec2(b.x + bw, b.y + px(4.0f)), theme::Track, px(2.0f));
            draw::rect(ldl, b, ImVec2(b.x + bw * std::clamp(a.percent / 100.0f, 0.0f, 1.0f), b.y + px(4.0f)), theme::Accent, px(2.0f));
        }
        ImGui::Dummy(ImVec2(rowW, rowH));
        ImGui::PopID();
    }
    if (s.total == 0)
        text("Pas encore de succès pour ce jeu.", 14.0f, Weight::Medium, theme::Muted);
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return open;
}

} // namespace ui
