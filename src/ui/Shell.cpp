// Shell : la fenêtre principale de l'interface. Dessine la barre du haut
// (logo, navigation en pilule, recherche, bouton Ajouter, avatar) puis
// l'écran courant en dessous.

#include "../ExternalSetup.hpp"
#include "../Trash.hpp"
#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <SDL3/SDL.h>

#include <ctime>
#include <system_error>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

namespace {

struct NavItem {
    const char* label;
    Screen screen;
};
constexpr NavItem kNav[] = {{"Accueil", Screen::Home},
                            {"Bibliothèque", Screen::Library},
                            {"Amis", Screen::Friends},
                            {"BIOS", Screen::Bios},
                            {"Paramètres", Screen::Settings}};

// La fiche d'un jeu fait partie de la bibliothèque.
Screen tabOf(Screen s)
{
    return s == Screen::Game ? Screen::Library : s;
}

} // namespace

void Shell::draw(UiContext& ctx)
{
    // Big Picture : un écran à part, qui occupe toute la fenêtre.
    if (ctx.nav.bigPicture) {
        m_bigPicture.draw(ctx);
        return;
    }
    // Le bouton Start de la manette ouvre le mode Big Picture (sauf pendant
    // une fenêtre surgissante ou la saisie d'un texte).
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        ctx.setBigPicture(true);
        return;
    }
    // Une fenêtre ImGui sans bordure qui occupe toute la fenêtre Windows.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    // Coins carrés : avec des coins arrondis, le fond noir d'OpenGL
    // apparaissait dans les quatre coins de la fenêtre.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##app", nullptr, flags);
    ImGui::PopStyleVar();

    drawTopBar(ctx);

    // La page : marges de 40 px sur les côtés, défilement si elle dépasse.
    ImGui::SetCursorPos(ImVec2(px(40.0f), px(76.0f) + px(12.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##page", ImVec2(ImGui::GetContentRegionAvail().x - px(40.0f), ImGui::GetContentRegionAvail().y - px(28.0f)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    switch (ctx.nav.screen) {
    case Screen::Home: m_home.draw(ctx); break;
    case Screen::Library: m_library.draw(ctx); break;
    case Screen::Game: m_game.draw(ctx); break;
    case Screen::AddGames: m_addGames.draw(ctx); break;
    case Screen::Bios: m_bios.draw(ctx); break;
    case Screen::Friends: m_friends.draw(ctx); break;
    case Screen::Settings: m_settings.draw(ctx); break;
    }
    ImGui::EndChild();

    ImGui::End();
}

void Shell::drawTopBar(UiContext& ctx)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowSize().x;
    const float barHeight = px(76.0f);
    const float centerY = origin.y + barHeight / 2;

    // Logo : carré noir avec un triangle "lecture", puis le nom.
    ImGui::SetCursorPos(ImVec2(px(40.0f), (barHeight - px(30.0f)) / 2));
    // Logo : l'icône de Nexo Retro, puis son nom. Un clic ramène à l'accueil.
    const float logoWidth = px(30.0f + 10.0f + 5.0f) + draw::measure(Weight::ExtraBold, 17.0f, "Nexo").x +
                            draw::measure(Weight::ExtraBold, 17.0f, "Retro").x;
    if (ImGui::InvisibleButton("##logo", ImVec2(logoWidth, px(30.0f))))
        ctx.nav.go(Screen::Home);
    brand(ctx, dl, ImGui::GetItemRectMin(), 30.0f, 17.0f, theme::Ink);

    // Navigation en pilule, centrée.
    const float itemPad = px(18.0f);
    float navWidth = px(8.0f);
    for (const NavItem& item : kNav)
        navWidth += draw::measure(Weight::SemiBold, 14.0f, item.label).x + itemPad * 2 + px(2.0f);
    const float navHeight = px(44.0f);
    const ImVec2 navMin(origin.x + (width - navWidth) / 2, centerY - navHeight / 2);
    draw::rect(dl, navMin, ImVec2(navMin.x + navWidth, navMin.y + navHeight), theme::Surface, navHeight / 2, theme::Hairline);
    float x = navMin.x + px(4.0f);
    for (const NavItem& item : kNav) {
        const ImVec2 ts = draw::measure(Weight::SemiBold, 14.0f, item.label);
        const ImVec2 size(ts.x + itemPad * 2, navHeight - px(8.0f));
        ImGui::SetCursorScreenPos(ImVec2(x, navMin.y + px(4.0f)));
        ImGui::PushID(item.label);
        if (ImGui::InvisibleButton("##nav", size)) {
            ctx.nav.go(item.screen);
            if (item.screen == Screen::Library)
                ctx.nav.librarySystem = "all";
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool active = tabOf(ctx.nav.screen) == item.screen;
        const ImVec2 min = ImGui::GetItemRectMin();
        if (active)
            draw::rect(dl, min, ImVec2(min.x + size.x, min.y + size.y), theme::Ink, size.y / 2);
        else if (hovered)
            draw::rect(dl, min, ImVec2(min.x + size.x, min.y + size.y), theme::Ground, size.y / 2);
        draw::text(dl, ImVec2(min.x + itemPad, min.y + (size.y - ts.y) / 2), Weight::SemiBold, 14.0f,
                   active ? theme::Surface : (hovered ? theme::Ink : theme::Muted), item.label);
        x += size.x + px(2.0f);
    }

    // Une nouvelle version de Nexo Retro : une pastille discrète à droite du
    // logo, qui mène à Paramètres > Mises à jour. (Seulement s'il y a la
    // place avant la navigation, et pas pour une version ignorée.)
    if (const AppUpdater* updater = ctx.appUpdater) {
        const AppUpdater::State state = updater->state();
        const bool found = state == AppUpdater::State::Available || state == AppUpdater::State::Downloading ||
                           state == AppUpdater::State::Ready;
        if (found && updater->latestVersion() != ctx.settings.appIgnoredVersion) {
            const char* label = state == AppUpdater::State::Ready ? "Mise à jour prête" : "Mise à jour disponible";
            const ImVec2 ts = draw::measure(Weight::Bold, 13.0f, label);
            const ImVec2 size(ts.x + px(16.0f + 14.0f + 6.0f + 14.0f), px(32.0f));
            const ImVec2 at(origin.x + px(40.0f) + logoWidth + px(18.0f), centerY - size.y / 2);
            if (at.x + size.x + px(12.0f) < navMin.x) {
                ImGui::SetCursorScreenPos(at);
                if (ImGui::InvisibleButton("##appUpdate", size)) {
                    ctx.nav.go(Screen::Settings);
                    ctx.nav.settingsTab = 10; // l'onglet "Mises à jour"
                }
                const bool hovered = ImGui::IsItemHovered();
                draw::rect(dl, at, ImVec2(at.x + size.x, at.y + size.y), hovered ? theme::Accent : theme::AccentSoft,
                           size.y / 2);
                const ImU32 ink = hovered ? theme::Surface : theme::Accent;
                draw::icon(dl, draw::Icon::Refresh, ImVec2(at.x + px(14.0f), centerY - px(7.0f)), px(14.0f), ink);
                draw::text(dl, ImVec2(at.x + px(14.0f + 14.0f + 6.0f), centerY - ts.y / 2), Weight::Bold, 13.0f, ink, label);
            }
        }
    }

    // À droite : recherche, Ajouter, avatar.
    const float addWidth = draw::measure(Weight::Bold, 14.0f, "Ajouter").x + px(18.0f * 2 + 17.0f + 8.0f);
    float right = origin.x + width - px(40.0f);
    const float avatarSize = px(40.0f);
    right -= avatarSize;
    const std::string initial = ctx.settings.nickname.empty() ? "?" : ctx.settings.nickname.substr(0, 1);
    avatar(dl, ImVec2(right + avatarSize / 2, centerY), avatarSize / 2, initial.c_str(), theme::rgb(0xE8E4FF),
           theme::rgb(0x3B2DB8));
    right -= px(10.0f) + addWidth;
    ImGui::SetCursorScreenPos(ImVec2(right, centerY - px(20.0f)));
    const bool onAddPage = ctx.nav.screen == Screen::AddGames;
    if (onAddPage) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw::rect(dl, ImVec2(p.x - px(4.0f), p.y - px(4.0f)), ImVec2(p.x + addWidth + px(4.0f), p.y + px(44.0f)),
                   theme::withAlpha(theme::Accent, 46), px(24.0f));
    }
    if (button("Ajouter##top", {.style = ButtonStyle::Accent, .icon = draw::Icon::Plus, .height = 40.0f}))
        ctx.nav.go(Screen::AddGames);
    right -= px(10.0f + 40.0f);
    ImGui::SetCursorScreenPos(ImVec2(right, centerY - px(20.0f)));
    if (iconButton("##search", draw::Icon::Search, ButtonStyle::Outline, 40.0f, "Rechercher un jeu")) {
        ctx.nav.go(Screen::Library);
        ctx.nav.librarySystem = "all";
        ctx.nav.focusSearch = true;
    }
    right -= px(10.0f + 40.0f);
    ImGui::SetCursorScreenPos(ImVec2(right, centerY - px(20.0f)));
    if (iconButton("##bigpicture", draw::Icon::Tv, ButtonStyle::Outline, 40.0f, "Mode Big Picture (Start à la manette)"))
        ctx.setBigPicture(true);
}

void Shell::onDialogResult(UiContext& ctx, const DialogResult& result)
{
    if (result.paths.empty())
        return;
    switch (result.purpose) {
    case DialogPurpose::AddGameFiles:
    case DialogPurpose::AddGameFolder:
        ctx.nav.go(Screen::AddGames);
        m_addGames.import(ctx, result.paths);
        break;
    case DialogPurpose::AddBiosFile:
    case DialogPurpose::AddBiosFolder:
        m_bios.import(ctx, result.paths);
        break;
    case DialogPurpose::ChooseCover: {
        const Game* game = ctx.library.find(ctx.nav.game);
        if (!game)
            break;
        // On copie l'image dans covers/ : la jaquette reste disponible même
        // si l'original est déplacé. Le nom contient l'heure pour que le
        // cache d'images ne réutilise pas une ancienne version.
        std::error_code ec;
        const fs::path source = result.paths.front();
        fs::create_directories(ctx.dir("covers"), ec);
        const fs::path target = ctx.dir("covers") / fromUtf8(toUtf8(game->path.stem()) + "-" +
                                                              std::to_string(std::time(nullptr)) +
                                                              toUtf8(source.extension()));
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ctx.notify("Impossible de copier l'image : " + ec.message());
            break;
        }
        ctx.userData.record(game->path).cover = toUtf8(target);
        ctx.userData.save();
        ctx.notify("Jaquette mise à jour");
        break;
    }
    case DialogPurpose::ChooseExternal: {
        // Le programme d'un émulateur externe (Ryujinx.exe...), pour une console.
        if (ctx.nav.externalChoosing.empty())
            break;
        ctx.settings.externalPaths[ctx.nav.externalChoosing] = toUtf8(result.paths.front());
        ctx.settingsChanged();
        ctx.notify("Émulateur enregistré : " + toUtf8(result.paths.front().filename()));
        break;
    }
    case DialogPurpose::ImportSystemFiles: {
        // Firmware PS3, clés Switch, BIOS Xbox : rangés et branchés sur l'émulateur.
        const System* system = Library::systemById(ctx.nav.externalChoosing);
        if (!system)
            break;
        ctx.notify(external::importSystemFiles(system->id, ctx.library.externalPath(*system), ctx.dir("bios"), result.paths));
        break;
    }
    case DialogPurpose::ImportSave: {
        // Remplacer la sauvegarde du jeu par un autre fichier (celle d'un ami,
        // d'un autre émulateur...). L'ancienne part à la Corbeille : une
        // erreur se rattrape.
        const Game* game = ctx.library.find(ctx.nav.game);
        if (!game)
            break;
        const fs::path source = result.paths.front();
        const fs::path target = gameSaveFile(ctx, *game);
        std::error_code ec;
        if (fs::equivalent(source, target, ec))
            break;
        fs::create_directories(target.parent_path(), ec);
        std::string error;
        const bool hadSave = fs::exists(target, ec);
        if (hadSave && !moveToTrash({target}, error)) {
            ctx.notify("Impossible de mettre l'ancienne sauvegarde à la Corbeille : " + error);
            break;
        }
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec)
            ctx.notify("Impossible de copier la sauvegarde : " + ec.message());
        else
            ctx.notify(hadSave ? "Sauvegarde remplacée (l'ancienne est dans la Corbeille)" : "Sauvegarde ajoutée");
        break;
    }
    }
}

void Shell::onDrop(UiContext& ctx, const std::vector<fs::path>& paths)
{
    // Sur l'écran BIOS, un fichier déposé est un BIOS ; partout ailleurs,
    // c'est un jeu (ou un dossier de jeux).
    if (ctx.nav.screen == Screen::Bios) {
        m_bios.import(ctx, paths);
    } else {
        ctx.nav.go(Screen::AddGames);
        m_addGames.import(ctx, paths);
    }
}

} // namespace ui
