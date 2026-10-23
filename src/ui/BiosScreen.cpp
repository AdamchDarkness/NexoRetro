// BIOS : l'état de chaque console et une zone pour déposer un BIOS.

#include "Screens.hpp"
#include "Widgets.hpp"

#include <algorithm>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

namespace {

struct Status {
    const char* label;
    Tone tone;
};

Status statusOf(const BiosSystem& s)
{
    const int present = static_cast<int>(std::count_if(s.files.begin(), s.files.end(), [](const BiosFile& f) { return f.present; }));
    const int verified = static_cast<int>(
        std::count_if(s.files.begin(), s.files.end(), [](const BiosFile& f) { return f.check == BiosCheck::Verified; }));
    // Des fichiers sont là, mais aucun n'a la bonne empreinte : la console
    // risque de ne pas démarrer.
    if (present > 0 && verified == 0)
        return {"À vérifier", Tone::Warning};
    switch (s.need) {
    case BiosNeed::Required: return present > 0 ? Status{"Prêt", Tone::Success} : Status{"Manquant", Tone::Danger};
    case BiosNeed::Recommended: return present > 0 ? Status{"Prêt", Tone::Success} : Status{"Recommandé", Tone::Warning};
    case BiosNeed::Optional:
        if (present == static_cast<int>(s.files.size()))
            return {"Prêt", Tone::Success};
        return present > 0 ? Status{"Partiel", Tone::Neutral} : Status{"Facultatif", Tone::Neutral};
    case BiosNeed::None: return {"Rien à faire", Tone::Neutral};
    }
    return {"", Tone::Neutral};
}

float cardHeight(const BiosSystem& s)
{
    return px(18.0f * 2 + 38.0f) + (s.files.empty() ? 0.0f : px(12.0f) + px(46.0f) * s.files.size());
}

void systemCard(const BiosSystem& s, ImVec2 min, float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = cardHeight(s);
    draw::rect(dl, min, ImVec2(min.x + width, min.y + h), theme::Surface, px(20.0f));
    const float pad = px(18.0f);
    draw::rect(dl, ImVec2(min.x + pad, min.y + pad), ImVec2(min.x + pad + px(34.0f), min.y + pad + px(34.0f)),
               theme::rgb(s.color), px(10.0f));
    const Status status = statusOf(s);
    const ImVec2 ps = pillSize(status.label);
    const float textW = width - pad * 2 - px(46.0f) - ps.x - px(10.0f);
    draw::textEllipsis(dl, ImVec2(min.x + pad + px(46.0f), min.y + pad), Weight::ExtraBold, 15.0f, theme::Ink, s.name, textW);
    draw::textEllipsis(dl, ImVec2(min.x + pad + px(46.0f), min.y + pad + px(20.0f)), Weight::Medium, 12.0f, theme::Muted,
                       s.needText, textW);
    pillAt(dl, ImVec2(min.x + width - pad - ps.x, min.y + pad + px(6.0f)), status.label, status.tone);

    float y = min.y + pad + px(38.0f) + px(12.0f);
    for (const BiosFile& f : s.files) {
        const ImVec2 a(min.x + pad, y);
        const ImVec2 b(min.x + width - pad, y + px(40.0f));
        if (f.present && f.check == BiosCheck::Verified) {
            draw::rect(dl, a, b, theme::rgb(0xF4FAF6), px(12.0f), theme::rgb(0xD3EBDC));
            draw::icon(dl, draw::Icon::Check, ImVec2(a.x + px(12.0f), a.y + px(12.0f)), px(16.0f), theme::Success);
        } else if (f.present) {
            // Présent mais pas le bon contenu (abîmé, mal nommé...) : un "!".
            draw::rect(dl, a, b, theme::WarningSoft, px(12.0f));
            const ImVec2 c(a.x + px(20.0f), a.y + px(20.0f));
            dl->AddCircleFilled(c, px(8.0f), theme::Warning, 16);
            const ImVec2 bang = draw::measure(Weight::ExtraBold, 11.0f, "!");
            draw::text(dl, ImVec2(c.x - bang.x / 2, c.y - bang.y / 2), Weight::ExtraBold, 11.0f, theme::Surface, "!");
        } else {
            draw::rect(dl, a, b, theme::Surface, px(12.0f));
            draw::dashedRect(dl, a, b, theme::Dashed, px(12.0f), px(1.0f));
            draw::icon(dl, draw::Icon::Plus, ImVec2(a.x + px(12.0f), a.y + px(12.0f)), px(16.0f), theme::Muted);
        }
        const float noteW = draw::measure(Weight::Medium, 12.0f, f.note).x;
        draw::textEllipsis(dl, ImVec2(a.x + px(38.0f), a.y + px(11.0f)), Weight::Bold, 13.0f, theme::Ink, f.label,
                           b.x - a.x - px(56.0f) - noteW);
        draw::text(dl, ImVec2(b.x - px(12.0f) - noteW, a.y + px(12.0f)), Weight::Medium, 12.0f, theme::Muted, f.note);
        y += px(46.0f);
    }
}

} // namespace

void BiosScreen::import(UiContext& ctx, const std::vector<fs::path>& paths)
{
    // Un dossier déposé (ou choisi) : on y cherche tous les BIOS reconnus.
    std::error_code ec;
    for (const fs::path& p : paths)
        ctx.notify(fs::is_directory(p, ec) ? ctx.bios.importFolder(p) : ctx.bios.import(p));
}

void BiosScreen::draw(UiContext& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float asideW = px(360.0f);
    const float leftW = avail.x - asideW - px(24.0f);
    const auto& systems = ctx.bios.systems();

    ImGui::BeginGroup();
    const ImVec2 head = ImGui::GetCursorPos();
    pageTitle("BIOS", "Le logiciel interne de votre console. Certaines consoles en ont besoin pour démarrer.");
    const ImVec2 afterTitle = ImGui::GetCursorPos();

    int ready = 0, todo = 0;
    for (const BiosSystem& s : systems) {
        const Status st = statusOf(s);
        if (s.need == BiosNeed::None)
            continue;
        (st.tone == Tone::Success ? ready : todo) += 1;
    }
    const std::string readyText = std::to_string(ready) + (ready > 1 ? " prêts" : " prêt");
    const std::string todoText = std::to_string(todo) + " à compléter";
    const float pillsW = pillSize(readyText.c_str(), 13.0f).x + px(8.0f) + pillSize(todoText.c_str(), 13.0f).x;
    ImGui::SetCursorPos(ImVec2(head.x + leftW - pillsW, head.y + px(34.0f)));
    pill(readyText.c_str(), Tone::Success, 13.0f);
    sameLine(8.0f);
    pill(todoText.c_str(), Tone::Warning, 13.0f);
    ImGui::SetCursorPos(ImVec2(afterTitle.x, afterTitle.y + px(14.0f)));

    // Deux colonnes "en maçonnerie" : chaque carte va dans la colonne la
    // moins haute, pour éviter les trous.
    const float gap = px(16.0f);
    const float colW = (leftW - gap) / 2;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float colY[2] = {origin.y, origin.y};
    for (const BiosSystem& s : systems) {
        const int c = colY[0] <= colY[1] ? 0 : 1;
        systemCard(s, ImVec2(origin.x + c * (colW + gap), colY[c]), colW);
        colY[c] += cardHeight(s) + gap;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(leftW, std::max(colY[0], colY[1]) - origin.y));
    ImGui::EndGroup();

    // --- Colonne de droite.
    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginGroup();
    space(70.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 zone = ImGui::GetCursorScreenPos();
    const ImVec2 zoneMax(zone.x + asideW, zone.y + px(306.0f));
    draw::rect(dl, zone, zoneMax, theme::SurfaceAlt, px(24.0f));
    draw::dashedRect(dl, zone, zoneMax, theme::Dashed, px(24.0f), px(2.0f));
    const float cx = zone.x + asideW / 2;
    dl->AddCircleFilled(ImVec2(cx, zone.y + px(56.0f)), px(26.0f), theme::AccentSoft, 32);
    draw::icon(dl, draw::Icon::Chip, ImVec2(cx - px(12.0f), zone.y + px(44.0f)), px(24.0f), theme::Accent);
    const char* title = "Déposer un BIOS";
    const ImVec2 ts = draw::measure(Weight::ExtraBold, 17.0f, title);
    draw::text(dl, ImVec2(cx - ts.x / 2, zone.y + px(94.0f)), Weight::ExtraBold, 17.0f, theme::Ink, title);
    draw::textWrapped(dl, ImVec2(zone.x + px(28.0f), zone.y + px(122.0f)), Weight::Medium, 13.0f, theme::Muted,
                      "Glissez un fichier ou un dossier dans cette fenêtre : chaque BIOS est reconnu et rangé au bon endroit.",
                      asideW - px(56.0f));
    const float bw = asideW - px(56.0f);
    ImGui::SetCursorScreenPos(ImVec2(zone.x + px(28.0f), zone.y + px(196.0f)));
    if (button("Choisir des fichiers", {.style = ButtonStyle::Primary, .height = 40.0f, .width = bw}))
        ctx.openDialog(DialogPurpose::AddBiosFile);
    ImGui::SetCursorScreenPos(ImVec2(zone.x + px(28.0f), zone.y + px(244.0f)));
    if (button("Chercher dans un dossier", {.style = ButtonStyle::Outline, .icon = draw::Icon::Search, .height = 40.0f, .width = bw}))
        ctx.openDialog(DialogPurpose::AddBiosFolder);
    ImGui::SetCursorScreenPos(ImVec2(zone.x, zoneMax.y));
    space(20.0f);

    beginCard("##how", ImVec2(asideW, px(270.0f)), 22.0f);
    const float w = ImGui::GetContentRegionAvail().x;
    text("Comment ça marche ?", 16.0f, Weight::ExtraBold);
    textWrapped("Chaque BIOS est reconnu à son contenu (son empreinte), même mal nommé, puis copié sous le nom et dans "
                "le dossier attendus par l'émulateur. Un fichier abîmé est signalé par un « ! ».",
                13.0f, Weight::Medium, theme::Body, w);
    space(2.0f);
    textWrapped("Les BIOS ne sont jamais fournis : utilisez celui de votre propre console.", 13.0f, Weight::Medium,
                theme::Body, w);
    space(4.0f);
    if (button("Ouvrir le dossier des BIOS", {.style = ButtonStyle::Outline, .icon = draw::Icon::Folder, .height = 40.0f, .width = -1.0f}))
        openFolder(ctx.bios.dir());
    endCard();
    ImGui::EndGroup();
}

} // namespace ui
