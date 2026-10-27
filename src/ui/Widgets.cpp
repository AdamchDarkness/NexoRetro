#include "Widgets.hpp"

#include "../Lang.hpp"

#include <imgui_internal.h> // ImGui::FindRenderedTextEnd

#include <algorithm>
#include <cmath>
#include <string>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

struct Colors {
    ImU32 fill, fillHover, text, border;
};

Colors colorsFor(ButtonStyle style)
{
    switch (style) {
    case ButtonStyle::Primary: return {theme::Ink, theme::InkHover, theme::Surface, 0};
    case ButtonStyle::Accent: return {theme::Accent, theme::AccentHover, theme::OnAccent, 0};
    case ButtonStyle::Outline: return {theme::Surface, theme::SurfaceAlt, theme::Ink, theme::Hairline};
    case ButtonStyle::Soft: return {theme::Ground, theme::NeutralSoft, theme::Ink, 0};
    case ButtonStyle::Ghost: return {0, theme::withAlpha(theme::Ink, 12), theme::Muted, 0};
    case ButtonStyle::Danger: return {theme::Danger, theme::mix(theme::Danger, IM_COL32_BLACK, 0.18f), theme::OnAccent, 0};
    }
    return {};
}

// Partie visible d'un label "Texte##id".
std::string_view visibleLabel(const char* label)
{
    return std::string_view(label, ImGui::FindRenderedTextEnd(label) - label);
}

void tooltip(const char* text)
{
    if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::PushFont(theme::font(Weight::SemiBold), px(13.0f));
        ImGui::SetTooltip("%s", std::string(lang::tr(text)).c_str());
        ImGui::PopFont();
    }
}

} // namespace

bool button(const char* label, const ButtonOptions& o)
{
    const std::string_view shown = visibleLabel(label);
    const float iconSize = px(o.fontSize + 3.0f);
    const ImVec2 textSize = draw::measure(Weight::Bold, o.fontSize, shown);
    const float gap = (o.icon != draw::Icon::None && !shown.empty()) ? px(8.0f) : 0.0f;
    const float content = textSize.x + (o.icon != draw::Icon::None ? iconSize + gap : 0.0f);
    const float padding = shown.empty() ? 0.0f : px(o.height >= 48.0f ? 24.0f : 18.0f);

    float width = content + padding * 2;
    if (o.width > 0.0f)
        width = px(o.width);
    else if (o.width < 0.0f)
        width = ImGui::GetContentRegionAvail().x;
    const ImVec2 size(width, px(o.height));

    ImGui::BeginDisabled(o.disabled);
    // (EnableNav : sans lui, ImGui 1.92 ne laisse pas la manette ni le clavier
    // aller sur un InvisibleButton.)
    const bool clicked = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    tooltip(o.tooltip);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Colors c = colorsFor(o.style);
    ImU32 fill = (hovered || held) ? c.fillHover : c.fill;
    ImU32 textColor = c.text;
    if (o.disabled) {
        fill = o.style == ButtonStyle::Primary || o.style == ButtonStyle::Accent ? theme::Track : c.fill;
        textColor = o.style == ButtonStyle::Primary || o.style == ButtonStyle::Accent ? theme::Surface : theme::Muted;
    }
    const float rounding = size.y / 2.0f;
    draw::rect(dl, min, max, fill, rounding, c.border);

    float x = min.x + (size.x - content) / 2.0f;
    if (o.icon != draw::Icon::None) {
        draw::icon(dl, o.icon, ImVec2(x, min.y + (size.y - iconSize) / 2.0f), iconSize, textColor);
        x += iconSize + gap;
    }
    if (!shown.empty())
        draw::text(dl, ImVec2(x, min.y + (size.y - textSize.y) / 2.0f), Weight::Bold, o.fontSize, textColor, shown);
    return clicked && !o.disabled;
}

bool iconButton(const char* id, draw::Icon icon, ButtonStyle style, float size, const char* tip, bool disabled)
{
    ImGui::BeginDisabled(disabled);
    const bool clicked = ImGui::InvisibleButton(id, px(size, size), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    tooltip(tip);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Colors c = colorsFor(style);
    draw::rect(dl, min, max, hovered ? c.fillHover : c.fill, px(size) / 2.0f, c.border);
    const float iconSize = px(size * 0.42f);
    ImU32 color = c.text;
    if (style == ButtonStyle::Ghost)
        color = theme::Muted;
    if (disabled)
        color = theme::Track;
    draw::icon(dl, icon, ImVec2(min.x + (max.x - min.x - iconSize) / 2, min.y + (max.y - min.y - iconSize) / 2), iconSize,
               color);
    return clicked && !disabled;
}

bool link(const char* label, float size)
{
    const std::string_view shown = visibleLabel(label);
    const ImVec2 textSize = draw::measure(Weight::Bold, size, shown);
    const bool clicked = ImGui::InvisibleButton(label, textSize, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw::text(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), Weight::Bold, size,
               hovered ? theme::AccentHover : theme::Accent, shown);
    return clicked;
}

float chipWidth(const char* label, const char* count)
{
    const float fontSize = 13.0f;
    const float labelW = draw::measure(Weight::Bold, fontSize, label).x;
    const float countW = count ? draw::measure(Weight::SemiBold, fontSize, count).x : 0.0f;
    return px(14.0f + 8.0f + 8.0f) + labelW + (count ? px(8.0f) + countW : 0.0f) + px(14.0f);
}

bool chip(const char* id, const char* label, const char* count, ImU32 dot, bool selected)
{
    const float fontSize = 13.0f;
    const ImVec2 labelSize = draw::measure(Weight::Bold, fontSize, label);
    const ImVec2 countSize = count ? draw::measure(Weight::SemiBold, fontSize, count) : ImVec2(0, 0);
    const float dotSize = px(8.0f);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(chipWidth(label, count), px(38.0f)), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fill = selected ? theme::Ink : (hovered ? theme::SurfaceAlt : theme::Surface);
    const ImU32 fg = selected ? theme::Surface : theme::Ink;
    draw::rect(dl, min, max, fill, (max.y - min.y) / 2, selected ? theme::Ink : theme::Hairline);

    float x = min.x + px(14.0f);
    const float cy = (min.y + max.y) / 2;
    // Sur fond noir, une pastille noire serait invisible : on l'éclaircit.
    dl->AddCircleFilled(ImVec2(x + dotSize / 2, cy), dotSize / 2, (selected && dot == theme::Ink) ? theme::Surface : dot);
    x += dotSize + px(8.0f);
    draw::text(dl, ImVec2(x, cy - labelSize.y / 2), Weight::Bold, fontSize, fg, label);
    if (count) {
        x += labelSize.x + px(8.0f);
        draw::text(dl, ImVec2(x, cy - countSize.y / 2), Weight::SemiBold, fontSize, theme::withAlpha(fg, 190), count);
    }
    return clicked;
}

bool slider(const char* id, int* value, int minValue, int maxValue, float width, const char* suffix)
{
    // Une barre qu'on fait glisser (souris), ou gauche/droite à la manette.
    const float w = px(width);
    const float h = px(28.0f);
    ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const float knob = px(20.0f);
    const float left = min.x + knob / 2;
    const float right = min.x + w - knob / 2;
    const int before = *value;
    if (active) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f);
        *value = minValue + static_cast<int>(std::lround(t * (maxValue - minValue)));
    }
    if (ImGui::IsItemFocused()) {
        const int step = std::max(1, (maxValue - minValue) / 20);
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            *value = std::max(minValue, *value - step);
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            *value = std::min(maxValue, *value + step);
    }

    const float t = static_cast<float>(*value - minValue) / static_cast<float>(maxValue - minValue);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = min.y + h / 2;
    const float x = left + (right - left) * t;
    draw::rect(dl, ImVec2(left, cy - px(3.0f)), ImVec2(right, cy + px(3.0f)), theme::Track, px(3.0f));
    draw::rect(dl, ImVec2(left, cy - px(3.0f)), ImVec2(x, cy + px(3.0f)), theme::Accent, px(3.0f));
    draw::shadow(dl, ImVec2(x - knob / 2, cy - knob / 2), ImVec2(x + knob / 2, cy + knob / 2), knob / 2, px(2.0f), px(1.0f), 60);
    dl->AddCircleFilled(ImVec2(x, cy), knob / 2, theme::isDark() ? theme::Ink : theme::Surface); // (clair sur un thème sombre)
    dl->AddCircle(ImVec2(x, cy), knob / 2, (active || hovered) ? theme::Accent : theme::Hairline, 0, px(1.5f));
    // La valeur, à droite de la barre.
    if (suffix) {
        const std::string label = std::to_string(*value) + suffix;
        draw::text(dl, ImVec2(min.x + w + px(12.0f), cy - px(9.0f)), Weight::Bold, 14.0f, theme::Ink, label);
    }
    return *value != before;
}

bool toggle(const char* id, bool* value, bool disabled)
{
    ImGui::BeginDisabled(disabled);
    const bool clicked = ImGui::InvisibleButton(id, px(46.0f, 28.0f), ImGuiButtonFlags_EnableNav);
    ImGui::EndDisabled();
    if (clicked && !disabled)
        *value = !*value;

    // Petite animation : le rond glisse au lieu de sauter.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float t = storage->GetFloat(key, *value ? 1.0f : 0.0f);
    const float target = *value ? 1.0f : 0.0f;
    t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
    storage->SetFloat(key, t);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 off = ImGui::ColorConvertU32ToFloat4(theme::Track);
    const ImVec4 on = ImGui::ColorConvertU32ToFloat4(theme::Accent);
    ImU32 track = ImGui::GetColorU32(ImVec4(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t,
                                            off.z + (on.z - off.z) * t, 1.0f));
    if (disabled)
        track = theme::withAlpha(track, 110);
    draw::rect(dl, min, max, track, (max.y - min.y) / 2);
    const float knob = px(22.0f);
    const float x = min.x + px(3.0f) + (max.x - min.x - knob - px(6.0f)) * t;
    draw::shadow(dl, ImVec2(x, min.y + px(3.0f)), ImVec2(x + knob, min.y + px(3.0f) + knob), knob / 2, px(2.0f), px(1.0f), 60);
    // (Le rond reste clair sur un thème sombre.)
    dl->AddCircleFilled(ImVec2(x + knob / 2, min.y + px(3.0f) + knob / 2), knob / 2,
                        theme::isDark() ? theme::Ink : theme::Surface);
    return clicked && !disabled;
}

bool segmented(const char* id, std::span<const char* const> labels, int* current, bool disabled)
{
    ImGui::PushID(id);
    const float fontSize = 13.0f;
    const float height = px(40.0f);
    const float inner = px(3.0f);
    float width = inner * 2;
    for (const char* l : labels)
        width += draw::measure(Weight::Bold, fontSize, l).x + px(28.0f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw::rect(dl, origin, ImVec2(origin.x + width, origin.y + height), theme::Ground, height / 2);

    bool changed = false;
    float x = origin.x + inner;
    ImGui::BeginDisabled(disabled);
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        const float w = draw::measure(Weight::Bold, fontSize, labels[i]).x + px(28.0f);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y + inner));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##segment", ImVec2(w, height - inner * 2), ImGuiButtonFlags_EnableNav) && *current != i) {
            *current = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const bool selected = *current == i;
        if (selected) {
            draw::shadow(dl, min, max, (max.y - min.y) / 2, px(2.0f), px(1.0f), 40);
            draw::rect(dl, min, max, theme::Surface, (max.y - min.y) / 2);
        }
        const ImVec2 ts = draw::measure(Weight::Bold, fontSize, labels[i]);
        draw::text(dl, ImVec2(min.x + (w - ts.x) / 2, min.y + (max.y - min.y - ts.y) / 2), Weight::Bold, fontSize,
                   selected || hovered ? theme::Ink : theme::Muted, labels[i]);
        x += w;
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

bool combo(const char* id, std::span<const char* const> items, int* current, float width)
{
    ImGui::PushFont(theme::font(Weight::Bold), px(14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(16.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::SurfaceAlt);
    ImGui::SetNextItemWidth(px(width));
    bool changed = false;
    if (ImGui::BeginCombo(id, std::string(lang::tr(items[*current])).c_str())) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i); // (deux traductions identiques ne doivent pas se confondre)
            const bool picked = ImGui::Selectable(std::string(lang::tr(items[i])).c_str(), i == *current);
            ImGui::PopID();
            if (picked) {
                *current = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    return changed;
}

bool searchField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool focus)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushFont(theme::font(Weight::SemiBold), px(14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(42.0f), px(12.0f)));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, theme::Muted);
    ImGui::SetNextItemWidth(px(width));
    if (focus)
        ImGui::SetKeyboardFocusHere();
    const bool changed = ImGui::InputTextWithHint(id, std::string(lang::tr(hint)).c_str(), buffer, bufferSize);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopFont();
    const float iconSize = px(18.0f);
    const float height = ImGui::GetItemRectSize().y;
    draw::icon(ImGui::GetWindowDrawList(), draw::Icon::Search,
               ImVec2(origin.x + px(14.0f), origin.y + (height - iconSize) / 2), iconSize, theme::Muted);
    return changed;
}

bool textField(const char* id, char* buffer, size_t bufferSize, const char* hint, float width, bool disabled)
{
    ImGui::PushFont(theme::font(Weight::SemiBold), px(14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(16.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::Ground);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::Ground);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme::Ground);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, theme::Muted);
    ImGui::SetNextItemWidth(width > 0 ? px(width) : -FLT_MIN);
    ImGui::BeginDisabled(disabled);
    const bool changed = ImGui::InputTextWithHint(id, std::string(lang::tr(hint)).c_str(), buffer, bufferSize);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return changed;
}

namespace {
void toneColors(Tone tone, ImU32& bg, ImU32& fg)
{
    switch (tone) {
    case Tone::Success: bg = theme::SuccessSoft; fg = theme::Success; break;
    case Tone::Warning: bg = theme::WarningSoft; fg = theme::Warning; break;
    case Tone::Danger: bg = theme::DangerSoft; fg = theme::Danger; break;
    case Tone::Neutral: bg = theme::NeutralSoft; fg = theme::Muted; break;
    case Tone::Accent: bg = theme::AccentSoft; fg = theme::AccentInk; break;
    case Tone::Info: bg = theme::Ground; fg = theme::Muted; break;
    case Tone::Light: bg = theme::Surface; fg = theme::Muted; break;
    }
}
} // namespace

ImVec2 pillSize(const char* label, float fontSize)
{
    const ImVec2 ts = draw::measure(Weight::Bold, fontSize, label);
    return ImVec2(ts.x + px(22.0f), ts.y + px(10.0f));
}

void pillAt(ImDrawList* dl, ImVec2 pos, const char* label, Tone tone, float fontSize)
{
    ImU32 bg = 0, fg = 0;
    toneColors(tone, bg, fg);
    const ImVec2 size = pillSize(label, fontSize);
    draw::rect(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, size.y / 2);
    draw::text(dl, ImVec2(pos.x + px(11.0f), pos.y + px(5.0f)), Weight::Bold, fontSize, fg, label);
}

void pill(const char* label, Tone tone, float fontSize)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    pillAt(ImGui::GetWindowDrawList(), pos, label, tone, fontSize);
    ImGui::Dummy(pillSize(label, fontSize));
}

void avatar(ImDrawList* dl, ImVec2 center, float radius, const char* initial, ImU32 bg, ImU32 fg, ImU32 dot, ImU32 ring)
{
    dl->AddCircleFilled(center, radius, bg, 32);
    const float size = radius * 0.75f / theme::scale();
    const ImVec2 ts = draw::measure(Weight::ExtraBold, size, initial);
    draw::text(dl, ImVec2(center.x - ts.x / 2, center.y - ts.y / 2), Weight::ExtraBold, size, fg, initial);
    if (dot) {
        const float r = radius * 0.28f;
        const ImVec2 c(center.x + radius * 0.72f, center.y + radius * 0.72f);
        dl->AddCircleFilled(c, r + px(2.0f), ring, 16);
        dl->AddCircleFilled(c, r, dot, 16);
    }
}

void text(std::string_view value, float size, Weight weight, ImU32 color)
{
    value = lang::tr(value);
    ImGui::PushFont(theme::font(weight), px(size));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void textWrapped(std::string_view value, float size, Weight weight, ImU32 color, float wrapWidth)
{
    if (lang::rtl()) {
        // (Les lignes sont coupées avant d'être retournées : voir draw::rtlLines.)
        ImGui::PushFont(theme::font(weight), px(size));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        std::string lines;
        for (const std::string& line : draw::rtlLines(weight, size, value, wrapWidth))
            lines += (lines.empty() ? "" : "\n") + line;
        ImGui::TextUnformatted(lines.data(), lines.data() + lines.size());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        return;
    }
    value = lang::tr(value);
    ImGui::PushFont(theme::font(weight), px(size));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void overline(std::string_view value)
{
    text(value, 12.0f, Weight::Bold, theme::Muted);
}

void beginCard(const char* id, ImVec2 size, float padding, ImU32 fill, float rounding, bool scroll)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, fill);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(padding), px(padding)));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (scroll)
        flags = 0;
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void endCard()
{
    ImGui::EndChild();
}

void space(float designPixels)
{
    ImGui::Dummy(ImVec2(px(designPixels), px(designPixels)));
}

void sameLine(float spacing)
{
    ImGui::SameLine(0.0f, px(spacing));
}

} // namespace ui
