#include "PauseMenu.hpp"

#include "Widgets.hpp"

#include <imgui.h>

using theme::px;
using theme::Weight;

namespace ui {

PauseAction PauseMenu::draw(const std::string& gameTitle, const std::string& systemName, int& volume, bool& volumeChanged,
                            const OnlineMenu& online)
{
    volumeChanged = false;
    PauseAction action = PauseAction::None;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Assombrit le jeu figé en arrière-plan.
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
        IM_COL32(17, 17, 20, 130));

    const float width = px(380.0f);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    if (m_justOpened)
        ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(28.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(8.0f, 8.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##pause", nullptr, flags);

    overline("PAUSE");
    textWrapped(gameTitle, 24.0f, Weight::ExtraBold, theme::Ink, width - px(56.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    text(systemName, 14.0f, Weight::Medium, theme::Muted);
    space(6.0f);

    // [C++] Une lambda pour ne pas répéter "if (button) action = ..." 8 fois.
    auto item = [&](const char* label, PauseAction result, ButtonStyle style, draw::Icon icon) {
        if (button(label, {.style = style, .icon = icon, .height = 46.0f, .width = -1.0f}))
            action = result;
    };
    item("Reprendre", PauseAction::Resume, ButtonStyle::Primary, draw::Icon::Play);
    if (m_justOpened)
        ImGui::SetItemDefaultFocus();
#ifdef __ANDROID__
    // (Pas de clavier sur un téléphone : pas de raccourcis F2..., et l'appli
    // est toujours en plein écran.)
    item("Sauvegarder l'état", PauseAction::SaveState, ButtonStyle::Soft, draw::Icon::None);
    item("Charger l'état", PauseAction::LoadState, ButtonStyle::Soft, draw::Icon::None);
    item("Emplacements de sauvegarde", PauseAction::Slots, ButtonStyle::Soft, draw::Icon::Grid);
    item("Redémarrer", PauseAction::Reset, ButtonStyle::Soft, draw::Icon::Refresh);
    item("Capture d'écran", PauseAction::Screenshot, ButtonStyle::Soft, draw::Icon::Image);
#else
    item("Sauvegarder l'état (F2)", PauseAction::SaveState, ButtonStyle::Soft, draw::Icon::None);
    item("Charger l'état (F4)", PauseAction::LoadState, ButtonStyle::Soft, draw::Icon::None);
    item("Emplacements de sauvegarde", PauseAction::Slots, ButtonStyle::Soft, draw::Icon::Grid);
    item("Redémarrer (F8)", PauseAction::Reset, ButtonStyle::Soft, draw::Icon::Refresh);
    item("Capture d'écran (F12)", PauseAction::Screenshot, ButtonStyle::Soft, draw::Icon::Image);
    item("Plein écran (F11)", PauseAction::Fullscreen, ButtonStyle::Soft, draw::Icon::None);
#endif
    item("Commandes de la console", PauseAction::Controls, ButtonStyle::Soft, draw::Icon::Gamepad);
    if (online.online) {
        // En ligne : le micro, et le ping à l'écran.
        if (online.voice)
            item(online.micMuted ? "Réactiver mon micro" : "Couper mon micro", PauseAction::ToggleMic, ButtonStyle::Soft,
                 online.micMuted ? draw::Icon::Mic : draw::Icon::MicOff);
        item(online.netStats ? "Masquer le ping et les pertes" : "Afficher le ping et les pertes", PauseAction::ToggleNetStats,
             ButtonStyle::Soft, draw::Icon::Users);
    }
    // Le volume : une barre à faire glisser (ou gauche/droite à la manette).
    space(4.0f);
    text("Volume", 13.0f, Weight::Bold, theme::Muted);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
    volumeChanged = slider("##volume", &volume, 0, 100, (width - px(56.0f)) / theme::scale() - 56.0f, " %");
    space(4.0f);
    item("Retour à la bibliothèque", PauseAction::Library, ButtonStyle::Outline, draw::Icon::ChevronLeft);
    item("Quitter l'émulateur", PauseAction::Quit, ButtonStyle::Ghost, draw::Icon::None);

    // Bouton B de la manette : fermer le menu.
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        action = PauseAction::Resume;

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    m_justOpened = false;
    return action;
}

ControlsResult PauseMenu::drawControls(const std::string& subtitle, const std::vector<ControlRow>& rows,
                                       const std::array<int, 18>& table)
{
    ControlsResult result;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
        IM_COL32(17, 17, 20, 130));

    // Les boutons de la manette, par leur POSITION (les lettres changent
    // d'une marque à l'autre : le "A" Xbox est la "Croix" PlayStation).
    // L'ordre est celui des numéros du RetroPad ; "Aucun" en dernier.
    static const char* const sources[] = {
        "Bas (A Xbox · Croix PS)", "Gauche (X Xbox · Carré PS)", "Select / Back", "Start",
        "Croix : haut", "Croix : bas", "Croix : gauche", "Croix : droite",
        "Droite (B Xbox · Rond PS)", "Haut (Y Xbox · Triangle PS)", "L / LB", "R / RB",
        "L2 / LT", "R2 / RT", "Clic stick gauche", "Clic stick droit", "Aucun"};
    constexpr int kNoneIndex = 16;
    // Les sticks : 0 = gauche, 1 = droit, 2/3 = idem avec le vertical inversé.
    static const char* const sticks[] = {"Stick gauche", "Stick droit", "Stick gauche (vertical inversé)",
                                         "Stick droit (vertical inversé)", "Aucun"};
    constexpr int kNoStick = 4;

    const float width = px(620.0f);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, viewport->Size.y * 0.9f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(28.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(8.0f, 8.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##controls", nullptr, flags);

    overline("COMMANDES");
    text("Commandes de la console", 22.0f, Weight::ExtraBold, theme::Ink);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
    textWrapped(subtitle, 13.0f, Weight::Medium, theme::Muted, width - px(56.0f));
    space(6.0f);

    const float comboW = 260.0f;
    for (const ControlRow& row : rows) {
        ImGui::PushID(row.coreButton >= 0 ? row.coreButton : static_cast<int>(&row - rows.data()) + 100);
        const float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(y + px(9.0f));
        text(row.description, 14.0f, Weight::Bold, theme::Ink);
        ImGui::SameLine(width - px(56.0f) - px(comboW));
        ImGui::SetCursorPosY(y);
        if (row.coreButton >= 16) {
            // Un stick du core : quel stick de la manette le pilote.
            int current = table[row.coreButton] >= 0 ? table[row.coreButton] : kNoStick;
            if (combo("##stick", sticks, &current, comboW)) {
                result.coreButton = row.coreButton;
                result.source = current == kNoStick ? -1 : current;
            }
        } else if (row.coreButton >= 0) {
            int current = table[row.coreButton] >= 0 ? table[row.coreButton] : kNoneIndex;
            if (combo("##source", sources, &current, comboW)) {
                result.coreButton = row.coreButton;
                result.source = current == kNoneIndex ? -1 : current;
            }
        }
        ImGui::PopID();
    }

    space(8.0f);
    if (button("Revenir aux réglages d'origine", {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 44.0f}))
        result.reset = true;
    sameLine();
    if (button("Retour", {.style = ButtonStyle::Primary, .height = 44.0f}))
        result.back = true;

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return result;
}

} // namespace ui
