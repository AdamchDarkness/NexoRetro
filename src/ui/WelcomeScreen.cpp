// L'assistant du premier lancement : en cinq étapes, de l'émulateur vide à
// une bibliothèque prête à jouer (langue, dossier des jeux, émulateurs,
// BIOS). Il ne s'affiche qu'une fois (Settings::firstRunDone) ; on peut le
// passer à tout moment.

#include "../Lang.hpp"
#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <set>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

constexpr int kSteps = 5;

// Les émulateurs (cores) qui manquent pour les jeux de la bibliothèque.
std::vector<std::string> missingCores(const UiContext& ctx)
{
    std::set<std::string> cores;
    for (const Game& g : ctx.library.games())
        if (!g.system->external() && !ctx.library.hasCore(g))
            cores.insert(ctx.library.coreFileFor(g));
    return {cores.begin(), cores.end()};
}

// Les consoles de la bibliothèque dont le BIOS obligatoire manque.
std::vector<std::string> missingBios(const UiContext& ctx)
{
    std::set<std::string> names;
    for (const Game& g : ctx.library.games())
        if (const BiosSystem* b = ctx.bios.forSystem(g.system->id); b && b->need == BiosNeed::Required && !b->ready)
            names.insert(b->name);
    return {names.begin(), names.end()};
}

} // namespace

void WelcomeScreen::finish(UiContext& ctx)
{
    ImGui::CloseCurrentPopup(); // (toujours appelée depuis la fenêtre de l'assistant)
    ctx.settings.firstRunDone = true;
    ctx.settingsChanged();
    m_step = 0;
}

void WelcomeScreen::draw(UiContext& ctx)
{
    if (ctx.nav.welcomeStep >= 0) { // (tests : --welcome-step)
        m_step = std::clamp(ctx.nav.welcomeStep, 0, kSteps - 1);
        ctx.nav.welcomeStep = -1;
    }
    // Les émulateurs à installer, l'un après l'autre.
    if (!m_coreQueue.empty() && !ctx.installer.busy()) {
        ctx.installer.startCore(m_coreQueue.front(), ctx.library.coresDir());
        m_coreQueue.pop_front();
    }

    // Une fenêtre "modale" au centre : la page derrière est assombrie et
    // ne réagit plus tant que l'assistant est ouvert.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(std::min(px(760.0f), viewport->Size.x - px(32.0f)), std::min(px(560.0f), viewport->Size.y - px(32.0f)));
    if (!ImGui::IsPopupOpen("##welcome"))
        ImGui::OpenPopup("##welcome");
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 140));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(36.0f, 32.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (!ImGui::BeginPopupModal("##welcome", nullptr, ImGuiWindowFlags_NoDecoration)) {
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float inner = ImGui::GetContentRegionAvail().x;

    // En haut : les étapes (pastilles), et "Passer" à droite.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        for (int i = 0; i < kSteps; ++i) {
            const float w = i == m_step ? px(26.0f) : px(8.0f);
            float x = p.x;
            for (int k = 0; k < i; ++k)
                x += (k == m_step ? px(26.0f) : px(8.0f)) + px(6.0f);
            draw::rect(dl, ImVec2(x, p.y + px(6.0f)), ImVec2(x + w, p.y + px(14.0f)),
                       i <= m_step ? theme::Accent : theme::Track, px(4.0f));
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + inner - px(140.0f), p.y - px(6.0f)));
        if (m_step < kSteps - 1 && button("Passer l'assistant", {.style = ButtonStyle::Ghost, .height = 32.0f, .width = 140.0f,
                                                                 .fontSize = 12.0f}))
            finish(ctx);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(36.0f)));
    }

    const float bodyH = ImGui::GetContentRegionAvail().y - px(60.0f);
    ImGui::BeginChild("##welcomeBody", ImVec2(inner, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    bool canNext = true;
    switch (m_step) {
    case 0: {
        brand(ctx, ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 56.0f, 26.0f, theme::Ink);
        ImGui::Dummy(ImVec2(inner, px(66.0f)));
        text("Bienvenue !", 30.0f, Weight::ExtraBold);
        textWrapped("Nexo Retro réunit vos jeux de toutes les consoles dans une seule bibliothèque, et les lance en un clic. "
                    "Quelques étapes pour tout préparer : moins d'une minute.",
                    15.0f, Weight::Medium, theme::Body, inner);
        space(12.0f);
        overline("LANGUE");
        const std::vector<lang::Language> languages = lang::available(fromUtf8(SDL_GetBasePath()) / "assets" / "lang");
        std::vector<const char*> names;
        int current = 0;
        for (size_t i = 0; i < languages.size(); ++i) {
            names.push_back(languages[i].name.c_str());
            if (languages[i].code == ctx.settings.language)
                current = static_cast<int>(i);
        }
        if (combo("##welcomeLanguage", names, &current, 280.0f)) {
            ctx.settings.language = languages[static_cast<size_t>(current)].code;
            ctx.settingsChanged();
        }
        break;
    }
    case 1: {
        overline("ÉTAPE 1 · VOS JEUX");
        text("Où sont vos jeux ?", 26.0f, Weight::ExtraBold);
        textWrapped("Choisissez le dossier où vous rangez vos jeux : il devient un dossier surveillé, la console de chaque jeu "
                    "est reconnue toute seule. Vous pourrez en ajouter d'autres plus tard (bouton Ajouter).",
                    14.0f, Weight::Medium, theme::Body, inner);
        space(10.0f);
        if (button("Choisir le dossier de mes jeux", {.style = ButtonStyle::Primary, .icon = draw::Icon::Folder, .height = 46.0f}))
            ctx.openDialog(DialogPurpose::AddGameFolder);
        space(10.0f);
        const size_t count = ctx.library.games().size();
        if (count > 0)
            pill((std::to_string(count) + (count > 1 ? " jeux trouvés" : " jeu trouvé")).c_str(), Tone::Success, 13.0f);
        else
            text("Aucun jeu pour l'instant.", 13.0f, Weight::SemiBold, theme::Muted);
        space(10.0f);
        textWrapped("Nexo Retro ne fournit aucun jeu : utilisez les copies de vos propres jeux.", 12.0f, Weight::Medium,
                    theme::Muted, inner);
        break;
    }
    case 2: {
        overline("ÉTAPE 2 · ÉMULATEURS");
        text("Les émulateurs de vos consoles", 26.0f, Weight::ExtraBold);
        const std::vector<std::string> cores = missingCores(ctx);
        const bool working = ctx.installer.busy() || !m_coreQueue.empty();
        if (ctx.library.games().empty()) {
            textWrapped("Ajoutez d'abord des jeux : Nexo Retro proposera ensuite les émulateurs qu'il leur faut. Ils se "
                        "téléchargent aussi tout seuls au lancement d'un jeu.",
                        14.0f, Weight::Medium, theme::Body, inner);
        } else if (cores.empty() && !working) {
            textWrapped("Tous vos jeux ont leur émulateur. Ils seront gardés à jour automatiquement.", 14.0f, Weight::Medium,
                        theme::Body, inner);
            space(6.0f);
            pill("Prêt", Tone::Success, 13.0f);
        } else {
            textWrapped("Chaque console a son émulateur (un « core » libretro), téléchargé depuis le serveur officiel de "
                        "libretro. Il en manque pour vos jeux :",
                        14.0f, Weight::Medium, theme::Body, inner);
            space(6.0f);
            std::string list;
            for (const std::string& c : cores)
                list += (list.empty() ? "" : ", ") + c.substr(0, c.find("_libretro"));
            textWrapped(list, 13.0f, Weight::SemiBold, theme::Ink, inner);
            space(8.0f);
            if (working) {
                text(ctx.installer.busy() ? ctx.installer.stepText() : std::string("Préparation…"), 13.0f, Weight::SemiBold,
                     theme::Accent);
            } else if (button("Tout installer", {.style = ButtonStyle::Primary, .icon = draw::Icon::Upload, .height = 44.0f})) {
                m_coreQueue.assign(cores.begin(), cores.end());
            }
        }
        break;
    }
    case 3: {
        overline("ÉTAPE 3 · BIOS");
        text("Les BIOS de vos consoles", 26.0f, Weight::ExtraBold);
        textWrapped("Certaines consoles (PlayStation, Saturn, Neo Geo...) ont besoin du BIOS de la console : un fichier qui "
                    "vient de votre propre console. Nexo Retro ne le fournit pas, mais le reconnaît et le range tout seul.",
                    14.0f, Weight::Medium, theme::Body, inner);
        space(8.0f);
        const std::vector<std::string> bios = missingBios(ctx);
        if (bios.empty()) {
            pill("Aucun BIOS ne manque pour vos jeux", Tone::Success, 13.0f);
        } else {
            std::string list;
            for (const std::string& b : bios)
                list += (list.empty() ? "" : ", ") + b;
            text("BIOS manquant :", 13.0f, Weight::Bold, theme::Warning);
            textWrapped(list, 13.0f, Weight::SemiBold, theme::Ink, inner);
            space(8.0f);
            if (button("Ouvrir l'écran BIOS", {.style = ButtonStyle::Outline, .icon = draw::Icon::Chip, .height = 44.0f})) {
                ctx.nav.go(Screen::Bios);
                finish(ctx);
            }
        }
        break;
    }
    default: {
        overline("C'EST PRÊT");
        text("Bon jeu !", 30.0f, Weight::ExtraBold);
        space(4.0f);
        const char* tips[] = {"Échap (ou Select + Start) ouvre le menu pendant un jeu : sauvegardes, commandes, emplacements.",
                              "F2 sauvegarde l'état du jeu, F4 le recharge ; Retour arrière remonte le temps.",
                              "Le bouton Start de la manette ouvre le mode Big Picture, pour jouer sur la télé.",
                              "Onglet Amis : jouez en ligne avec un simple code, sans compte.",
                              "Onglet Speedrun : chrono, splits et vidéos de vos runs."};
        for (const char* tip : tips) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + px(6.0f), p.y + px(10.0f)), px(4.0f), theme::Accent, 12);
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(20.0f), p.y));
            textWrapped(tip, 14.0f, Weight::Medium, theme::Body, inner - px(20.0f));
            space(2.0f);
        }
        break;
    }
    }
    ImGui::EndChild();

    // En bas : Retour / Suivant (ou Terminer).
    const ImVec2 bottom = ImGui::GetCursorScreenPos();
    if (m_step > 0 && button("Retour", {.style = ButtonStyle::Soft, .icon = draw::Icon::ChevronLeft, .height = 44.0f, .width = 130.0f}))
        --m_step;
    ImGui::SetCursorScreenPos(ImVec2(bottom.x + inner - px(200.0f), bottom.y));
    if (m_step < kSteps - 1) {
        if (button(m_step == 0 ? "Commencer" : "Suivant",
                   {.style = ButtonStyle::Primary, .icon = draw::Icon::ChevronRight, .height = 44.0f, .width = 200.0f,
                    .disabled = !canNext}))
            ++m_step;
    } else if (button("Ouvrir la bibliothèque", {.style = ButtonStyle::Primary, .height = 44.0f, .width = 200.0f})) {
        ctx.nav.go(Screen::Library);
        finish(ctx);
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace ui
