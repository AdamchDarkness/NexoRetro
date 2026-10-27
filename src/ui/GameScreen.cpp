// Fiche du jeu : grande jaquette, informations, Jouer, favori, statistiques,
// sauvegardes d'état, et la fenêtre "Changer la jaquette".

#include "../Lang.hpp"
#include "../Trash.hpp"
#include "../Utf8.hpp"
#include "../WiiControllers.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

namespace {

// Petite carte de statistique : libellé gris + valeur en gras.
void statCard(const char* id, const char* label, const std::string& value, float width, ImU32 valueColor = theme::Ink,
              draw::Icon icon = draw::Icon::None)
{
    ImGui::PushID(id);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, px(76.0f)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw::rect(dl, min, ImVec2(min.x + width, min.y + px(76.0f)), theme::Surface, px(16.0f));
    draw::text(dl, ImVec2(min.x + px(16.0f), min.y + px(16.0f)), Weight::SemiBold, 12.0f, theme::Muted, label);
    float x = min.x + px(16.0f);
    if (icon != draw::Icon::None) {
        draw::icon(dl, icon, ImVec2(x, min.y + px(40.0f)), px(18.0f), valueColor);
        x += px(24.0f);
    }
    draw::textEllipsis(dl, ImVec2(x, min.y + px(36.0f)), Weight::ExtraBold, 20.0f, valueColor, value,
                       width - (x - min.x) - px(12.0f));
    ImGui::PopID();
}

// Carte d'une sauvegarde d'état (vignette + nom + date).
bool stateCard(const char* id, const char* name, long long time, float width)
{
    const bool exists = time > 0;
    ImGui::BeginDisabled(!exists);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, px(150.0f)), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    draw::rect(dl, min, ImVec2(min.x + width, min.y + px(150.0f)), hovered ? theme::SurfaceAlt : theme::Surface,
               px(16.0f), theme::Divider);
    const ImVec2 thumbMin(min.x + px(8.0f), min.y + px(8.0f));
    const ImVec2 thumbMax(min.x + width - px(8.0f), min.y + px(100.0f));
    draw::rect(dl, thumbMin, thumbMax, exists ? theme::Placeholder : theme::Ground, px(10.0f));
    if (exists) {
        const ImVec2 c((thumbMin.x + thumbMax.x) / 2, (thumbMin.y + thumbMax.y) / 2);
        draw::icon(dl, draw::Icon::Play, ImVec2(c.x - px(12.0f), c.y - px(12.0f)), px(24.0f), theme::withAlpha(theme::Ink, 110));
    } else {
        const ImVec2 ts = draw::measure(Weight::Bold, 12.0f, "Vide");
        draw::text(dl, ImVec2((thumbMin.x + thumbMax.x - ts.x) / 2, (thumbMin.y + thumbMax.y - ts.y) / 2), Weight::Bold,
                   12.0f, theme::Muted, "Vide");
    }
    draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(108.0f)), Weight::Bold, 13.0f, theme::Ink, name);
    draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(126.0f)), Weight::Medium, 12.0f, theme::Muted,
               exists ? formatAgo(time) : "Libre");
    return clicked && exists;
}

// Carte de la sauvegarde du jeu (.srm) : sa taille et sa date. Cliquer
// ouvre la fenêtre pour la voir, la remplacer ou la supprimer.
bool saveCard(const UiContext& ctx, const Game& game, float width)
{
    const fs::path file = gameSaveFile(ctx, game);
    const long long time = fileTime(file);
    std::error_code ec;
    const std::uintmax_t size = time > 0 ? fs::file_size(file, ec) : 0;
    const bool clicked = ImGui::InvisibleButton("##gameSave", ImVec2(width, px(150.0f)), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", std::string(lang::tr("Voir, remplacer ou supprimer la sauvegarde du jeu")).c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    draw::rect(dl, min, ImVec2(min.x + width, min.y + px(150.0f)), hovered ? theme::SurfaceAlt : theme::Surface, px(16.0f),
               theme::Divider);
    const ImVec2 thumbMin(min.x + px(8.0f), min.y + px(8.0f));
    const ImVec2 thumbMax(min.x + width - px(8.0f), min.y + px(100.0f));
    draw::rect(dl, thumbMin, thumbMax, time > 0 ? theme::AccentSoft : theme::Ground, px(10.0f));
    // Au centre : la taille du fichier (ou "Aucune").
    const std::string big = time > 0 ? formatSize(size) : "Aucune";
    const ImVec2 ts = draw::measure(Weight::ExtraBold, 18.0f, big);
    draw::text(dl, ImVec2((thumbMin.x + thumbMax.x - ts.x) / 2, (thumbMin.y + thumbMax.y - ts.y) / 2), Weight::ExtraBold, 18.0f,
               time > 0 ? theme::Accent : theme::Muted, big);
    draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(108.0f)), Weight::Bold, 13.0f, theme::Ink, "Sauvegarde du jeu");
    draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(126.0f)), Weight::Medium, 12.0f, theme::Muted,
               time > 0 ? formatAgo(time) : "Créée quand le jeu sauvegarde");
    return clicked;
}

// Nom lisible d'un core : "pcsx2_libretro.dll" -> "pcsx2".
std::string coreName(const std::string& file)
{
    std::string name = file;
    const size_t cut = name.find("_libretro");
    if (cut != std::string::npos)
        name.resize(cut);
    if (!name.empty())
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

} // namespace

void GameScreen::draw(UiContext& ctx)
{
    const Game* found = ctx.library.find(ctx.nav.game);
    if (!found) {
        ctx.nav.go(Screen::Library);
        return;
    }
    const Game& game = *found;
    GameRecord& record = ctx.userData.record(game.path);
    const bool hasCore = ctx.library.hasCore(game);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (button("Bibliothèque##back", {.style = ButtonStyle::Ghost, .icon = draw::Icon::ChevronLeft, .height = 34.0f}))
        ctx.nav.go(Screen::Library);
    space(4.0f);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float leftW = px(320.0f);

    // --- Colonne de gauche : jaquette et boutons.
    ImGui::BeginGroup();
    const ImVec2 coverMin = ImGui::GetCursorScreenPos();
    const float coverH = std::min(px(426.0f), avail.y - px(110.0f));
    const ImVec2 coverMax(coverMin.x + leftW, coverMin.y + coverH);
    draw::shadow(dl, coverMin, coverMax, px(20.0f), px(26.0f), px(14.0f), 42);
    ctx.covers.draw(dl, coverMin, coverMax, game, ctx.userData, px(20.0f), 38.0f);
    ImGui::Dummy(ImVec2(leftW, coverH));
    space(6.0f);
    if (button("Changer la jaquette", {.style = ButtonStyle::Outline, .icon = draw::Icon::Image, .width = 266.0f}))
        m_openPicker = true;
    sameLine(8.0f);
    const scrape::GameInfo* info = ctx.scraper.find(game.path);
    const bool searching = ctx.scraper.queued(game.path) || (ctx.scraper.pending() > 0 && ctx.scraper.current() == game.title);
    if (iconButton("##scrape", draw::Icon::Refresh, ButtonStyle::Outline, 44.0f,
                   searching ? "Recherche en cours…" : "Rechercher à nouveau la jaquette et les informations en ligne",
                   searching))
        ctx.scraper.request(game, true);
    const bool hasOnlineCover = info && !info->boxart.empty();
    const char* coverNote = !record.cover.empty() ? "Jaquette personnalisée"
                            : hasOnlineCover      ? "Jaquette trouvée en ligne"
                                                  : "Jaquette générée automatiquement";
    const float noteW = draw::measure(Weight::Medium, 12.0f, coverNote).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (leftW - noteW) / 2);
    text(coverNote, 12.0f, Weight::Medium, theme::Muted);
    ImGui::EndGroup();

    // --- Colonne de droite : informations.
    ImGui::SameLine(0.0f, px(48.0f));
    ImGui::BeginGroup();
    const float rightW = avail.x - leftW - px(48.0f);
    pill(game.system->name, Tone::Accent);
    if (!game.region.empty()) {
        sameLine(8.0f);
        pill(game.region.c_str(), Tone::Light);
    }
    sameLine(8.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
    if (button("Renommer", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Pencil, .height = 30.0f, .fontSize = 13.0f})) {
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", game.title.c_str());
        m_openRename = true;
    }
    ImGui::SameLine(0.0f, px(2.0f));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f)); // même hauteur que « Renommer »
    if (button("Supprimer##delete", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Trash, .height = 30.0f, .fontSize = 13.0f})) {
        m_deleteFiles = false; // par défaut, on ne touche pas au fichier
        m_openDelete = true;
    }
    space(4.0f);
    textWrapped(game.title, 56.0f, Weight::ExtraBold, theme::Ink, rightW);
    space(2.0f);

    // Faits : ceux trouvés en ligne, puis ceux tirés du fichier.
    struct Fact {
        const char* label;
        std::string value;
    };
    std::vector<Fact> facts;
    if (info) {
        if (!info->genre.empty())
            facts.push_back({"Genre", info->genre});
        if (info->year)
            facts.push_back({"Sortie", std::to_string(info->year)});
        if (!info->developer.empty())
            facts.push_back({"Développeur", info->developer});
        if (!info->publisher.empty() && info->publisher != info->developer)
            facts.push_back({"Éditeur", info->publisher});
        if (info->players)
            facts.push_back({"Joueurs", info->players > 1 ? "1 à " + std::to_string(info->players) : "1"});
    }
    std::string format = toUtf8(game.path.extension());
    if (!format.empty())
        format.erase(0, 1);
    for (char& c : format)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    facts.push_back({"Format", format});
    facts.push_back({"Taille", formatSize(game.size)});
    if (!game.region.empty())
        facts.push_back({"Région", game.region});
    // Sur une ou plusieurs lignes, selon la place.
    const float factsLeft = ImGui::GetCursorPosX();
    for (size_t i = 0; i < facts.size(); ++i) {
        const float w = std::max(draw::measure(Weight::SemiBold, 12.0f, facts[i].label).x,
                                 draw::measure(Weight::Bold, 14.0f, facts[i].value).x);
        if (i > 0) {
            sameLine(28.0f);
            if (ImGui::GetCursorPosX() + w > factsLeft + rightW) {
                ImGui::NewLine();
                ImGui::SetCursorPosX(factsLeft);
            }
        }
        ImGui::BeginGroup();
        text(facts[i].label, 12.0f, Weight::SemiBold, theme::Muted);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        text(facts[i].value, 14.0f, Weight::Bold);
        ImGui::EndGroup();
    }
    space(4.0f);
    // D'où viennent ces informations.
    std::string source;
    if (searching)
        source = "Recherche de la jaquette et des informations en ligne…";
    else if (!info)
        source = "Informations pas encore cherchées : bouton sous la jaquette.";
    else if (info->status == scrape::GameInfo::Status::NotFound)
        source = "Jeu introuvable dans la base libretro. Vous pouvez choisir une jaquette à la main.";
    else
        source = std::string(info->byCrc ? "Reconnu avec certitude" : "Reconnu d'après le nom du fichier") +
                 " : " + info->name + " (base libretro)";
    textWrapped(source, 13.0f, Weight::Medium, theme::Muted, std::min(rightW, px(640.0f)));
    space(8.0f);

    // Actions.
    if (button("Jouer##play", {.style = ButtonStyle::Primary, .icon = draw::Icon::Play, .height = 54.0f, .fontSize = 16.0f,
                               .disabled = !hasCore, .tooltip = hasCore ? nullptr : "L'émulateur de cette console est manquant"}))
        ctx.play(game, {});
    sameLine();
    if (button("Jouer avec un ami##play", {.style = ButtonStyle::Outline, .icon = draw::Icon::Users, .height = 54.0f,
                                          .fontSize = 15.0f, .tooltip = "Créer un salon et inviter des amis"}))
        ctx.nav.go(Screen::Friends);
    sameLine();
    if (!game.system->external()) {
        // Codes de triche : le nombre de codes actifs, s'il y en a.
        int active = 0;
        for (const Cheat& c : cheatList(ctx, game))
            active += c.enabled;
        const std::string label = active ? "Codes de triche (" + std::to_string(active) + ")##cheats" : "Codes de triche##cheats";
        if (button(label.c_str(), {.style = ButtonStyle::Outline, .icon = draw::Icon::Chip, .height = 54.0f, .fontSize = 15.0f}))
            m_openCheats = true;
    }
    sameLine();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##favorite", px(54.0f, 54.0f), ImGuiButtonFlags_EnableNav)) {
            record.favorite = !record.favorite;
            ctx.userData.save();
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", std::string(lang::tr(record.favorite ? "Retirer des favoris" : "Ajouter aux favoris")).c_str());
        draw::rect(dl, p, ImVec2(p.x + px(54.0f), p.y + px(54.0f)), hovered ? theme::SurfaceAlt : theme::Surface, px(27.0f),
                   theme::Hairline);
        draw::icon(dl, record.favorite ? draw::Icon::Heart : draw::Icon::HeartOutline, ImVec2(p.x + px(16.0f), p.y + px(16.0f)),
                   px(22.0f), record.favorite ? theme::Favorite : theme::Muted);
    }

    // Wii : la manette émulée, jeu par jeu (elle change tout : un jeu à la
    // Wiimote seule ne répond pas au Nunchuk, et inversement).
    if (std::string_view(game.system->id) == "wii") {
        space(10.0f);
        std::vector<const char*> labels;
        int current = 0;
        const std::string_view chosen = record.controller.empty() ? kDefaultWiiController : std::string_view(record.controller);
        for (size_t i = 0; i < std::size(kWiiControllers); ++i) {
            labels.push_back(kWiiControllers[i].label);
            if (chosen == kWiiControllers[i].dolphin)
                current = static_cast<int>(i);
        }
        const float rowY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(rowY + px(10.0f)); // centré sur la hauteur du menu
        text("Manette Wii", 14.0f, Weight::Bold, theme::Ink);
        sameLine(12.0f);
        ImGui::SetCursorPosY(rowY);
        if (combo("##wiicontroller", labels, &current, 300.0f)) {
            record.controller = kWiiControllers[current].dolphin;
            ctx.userData.save();
        }
        textWrapped(kWiiControllers[current].help, 13.0f, Weight::Medium, theme::Muted, std::min(rightW, px(640.0f)));
    }

    // Émulateur manquant : un bandeau orange qui explique quoi faire.
    if (!hasCore && game.system->external()) {
        // Émulateur externe introuvable : dire lequel, et où l'indiquer.
        space(6.0f);
        beginCard("##missingExternal", ImVec2(std::min(rightW, px(720.0f)), px(64.0f)), 14.0f, theme::WarningSoft, 16.0f);
        text(std::string("Ce jeu se lance avec ") + game.system->externalName + " (" + game.system->externalExe +
                 "), un émulateur à installer à part.",
             13.0f, Weight::Bold, theme::Warning);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
        if (link("Indiquer où il est##external", 13.0f)) {
            ctx.nav.settingsTab = 8;
            ctx.nav.go(Screen::Settings);
        }
        endCard();
    } else if (!hasCore) {
        space(6.0f);
        beginCard("##missingCore", ImVec2(std::min(rightW, px(720.0f)), px(64.0f)), 14.0f, theme::WarningSoft, 16.0f);
        text(std::string(game.preferredCores ? "Ce jeu n'existe pas dans FBNeo, il lui faut MAME : placez "
                                             : "Émulateur manquant : placez ") +
                 ctx.library.coreFileFor(game) + " dans le dossier cores.",
             13.0f,
             Weight::Bold, theme::Warning);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
        // En un clic, depuis le serveur officiel de libretro (logiciels libres).
        const std::string coreFile = ctx.library.coreFileFor(game);
        if (ctx.installer.busySystem() == "core:" + coreFile) {
            const float p = ctx.installer.progress();
            text(ctx.installer.stepText() + (p >= 0.0f ? " " + std::to_string(static_cast<int>(p * 100)) + " %" : ""), 13.0f,
                 Weight::SemiBold, theme::Warning);
        } else if (link("Installer le core##dl", 13.0f)) {
            ctx.installer.startCore(coreFile, ctx.library.coresDir());
        }
        sameLine(16.0f);
        if (link("Ouvrir le dossier cores##open", 13.0f))
            openFolder(ctx.library.coresDir());
        endCard();
    } else if (const char* files = std::string_view(game.system->id) == "gc" || std::string_view(game.system->id) == "wii"
                                       ? "Dolphin"
                                   : std::string_view(game.system->id) == "psp" ? "PPSSPP"
                                                                                : nullptr;
               files && ctx.bios.forSystem(game.system->id) && !ctx.bios.forSystem(game.system->id)->files.empty() &&
               ctx.bios.forSystem(game.system->id)->files.front().check != BiosCheck::Verified) {
        // GameCube / Wii (Dolphin) et PSP (PPSSPP) : l'émulateur a besoin de
        // ses propres fichiers (libres, quelques Mo) : polices, réglages par jeu.
        space(6.0f);
        beginCard("##missingSystemFiles", ImVec2(std::min(rightW, px(720.0f)), px(64.0f)), 14.0f, theme::WarningSoft, 16.0f);
        text(std::string("Il manque les fichiers de ") + files +
                 " (polices, réglages par jeu…) : sans eux, textes manquants, écran noir ou plantage.",
             13.0f, Weight::Bold, theme::Warning);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
        if (ctx.installer.busySystem() == std::string("files:") + files) {
            const float p = ctx.installer.progress();
            text(ctx.installer.stepText() + (p >= 0.0f ? " " + std::to_string(static_cast<int>(p * 100)) + " %" : ""), 13.0f,
                 Weight::SemiBold, theme::Warning);
        } else if (link((std::string("Installer les fichiers de ") + files + "##sys").c_str(), 13.0f)) {
            ctx.installer.startSystemFiles(files, ctx.bios.dir());
        }
        endCard();
    }
    space(10.0f);

    // Statistiques.
    const float statGap = px(12.0f);
    const float statW = (rightW - statGap * 3) / 4;
    const BiosSystem* bios = ctx.bios.forSystem(game.system->id);
    std::string biosText = "Non requis";
    ImU32 biosColor = theme::Ink;
    draw::Icon biosIcon = draw::Icon::None;
    if (bios && bios->need != BiosNeed::None) {
        const bool any = std::any_of(bios->files.begin(), bios->files.end(), [](const BiosFile& f) { return f.present; });
        if (bios->ready && (any || bios->need == BiosNeed::Optional)) {
            biosText = any ? "Prêt" : "Facultatif";
            biosColor = any ? theme::Success : theme::Ink;
            biosIcon = any ? draw::Icon::Check : draw::Icon::None;
        } else {
            biosText = "Manquant";
            biosColor = bios->need == BiosNeed::Required ? theme::Danger : theme::Warning;
        }
    }
    statCard("time", "Temps de jeu", formatDuration(record.playSeconds), statW);
    ImGui::SameLine(0.0f, statGap);
    statCard("last", "Dernière partie", formatAgo(record.lastPlayed), statW);
    ImGui::SameLine(0.0f, statGap);
    statCard("core", "Émulateur",
             game.system->external() ? std::string(game.system->externalName) : coreName(ctx.library.coreFileFor(game)),
             statW);
    ImGui::SameLine(0.0f, statGap);
    statCard("bios", "BIOS", biosText, statW, biosColor, biosIcon);
    space(12.0f);

    // Émulateur externe : ses sauvegardes et ses états, c'est lui qui les gère.
    if (game.system->external()) {
        text("Sauvegardes", 18.0f, Weight::ExtraBold);
        textWrapped(std::string("Ce jeu tourne dans ") + game.system->externalName +
                        ", un émulateur externe : c'est lui qui gère ses sauvegardes, ses options et ses manettes.",
                    14.0f, Weight::Medium, theme::Muted, std::min(rightW, px(640.0f)));
    } else {
    // Sauvegardes d'état.
    const float cardW = (rightW - statGap * 3) / 4;
    // À droite des sauvegardes : une image trouvée en ligne (en jeu de préférence).
    const std::string* image = nullptr;
    const char* imageLabel = "";
    if (info && !info->snap.empty()) {
        image = &info->snap;
        imageLabel = "En jeu";
    } else if (info && !info->titleScreen.empty()) {
        image = &info->titleScreen;
        imageLabel = "Écran-titre";
    }
    const ImVec2 headingPos = ImGui::GetCursorPos();
    text("Sauvegardes", 18.0f, Weight::ExtraBold);
    {
        // À côté du titre : ouvrir le dossier des sauvegardes de CE jeu.
        const ImVec2 after = ImGui::GetCursorPos();
        const float titleW = draw::measure(Weight::ExtraBold, 18.0f, "Sauvegardes").x;
        ImGui::SetCursorPos(ImVec2(headingPos.x + titleW + px(12.0f), headingPos.y - px(4.0f)));
        if (button("Ouvrir le dossier de sauvegarde##saveDir",
                   {.style = ButtonStyle::Ghost, .icon = draw::Icon::Folder, .height = 30.0f, .fontSize = 13.0f}))
            openFolder(gameSaveDir(ctx, game));
        ImGui::SetCursorPos(after);
    }
    if (image) {
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(headingPos.x + (cardW + statGap) * 3, headingPos.y));
        text("Image du jeu", 18.0f, Weight::ExtraBold);
        ImGui::SetCursorPos(after);
    }
    // 1. La sauvegarde du jeu (la "pile" de la cartouche) : voir, remplacer.
    if (saveCard(ctx, game, cardW))
        m_openSave = true;
    ImGui::SameLine(0.0f, statGap);
    // 2. et 3. Les sauvegardes d'état : cliquer pour reprendre la partie.
    const fs::path autoState = autoStateFile(ctx, game);
    const fs::path quick = quickStateFile(ctx, game);
    if (stateCard("##auto", "État automatique", fileTime(autoState), cardW) && hasCore)
        ctx.play(game, autoState);
    ImGui::SameLine(0.0f, statGap);
    if (stateCard("##quick", "État rapide (F2)", fileTime(quick), cardW) && hasCore)
        ctx.play(game, quick);
    if (image) {
        const std::pair<const char*, const std::string*> images[] = {{imageLabel, image}};
        for (const auto& [label, file] : images) {
            ImGui::SameLine(0.0f, statGap);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cardW, px(150.0f)));
            draw::rect(dl, min, ImVec2(min.x + cardW, min.y + px(150.0f)), theme::Surface, px(16.0f), theme::Divider);
            const ImVec2 imgMin(min.x + px(8.0f), min.y + px(8.0f));
            const ImVec2 imgMax(min.x + cardW - px(8.0f), min.y + px(100.0f));
            if (!ctx.covers.drawImage(dl, imgMin, imgMax, *file, px(10.0f)))
                draw::rect(dl, imgMin, imgMax, theme::Placeholder, px(10.0f));
            draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(108.0f)), Weight::Bold, 13.0f, theme::Ink, label);
            draw::text(dl, ImVec2(min.x + px(12.0f), min.y + px(126.0f)), Weight::Medium, 12.0f, theme::Muted,
                       "Image officielle");
        }
    }
    } // (fin des sauvegardes d'un jeu émulé par un core)
    ImGui::EndGroup();

    drawCoverPicker(ctx, game);
    drawRename(ctx, game);
    drawDelete(ctx, game);
    drawSaveManager(ctx, game);
    drawCheats(ctx, game);

    // En tout dernier : retirer le jeu reconstruit la liste de la
    // bibliothèque, "game" n'est plus utilisable ensuite.
    if (m_removeRequested) {
        m_removeRequested = false;
        ctx.removeGame(game, m_deleteFiles);
    }
}

void GameScreen::drawDelete(UiContext& ctx, const Game& game)
{
    if (ctx.nav.openDelete) {
        ctx.nav.openDelete = false;
        m_deleteFiles = true; // (test : montre l'option cochée)
        m_openDelete = true;
    }
    if (m_openDelete) {
        ImGui::OpenPopup("##delete");
        m_openDelete = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(580.0f), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(30.0f, 30.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##delete", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float width = ImGui::GetContentRegionAvail().x;
        textWrapped("Supprimer « " + game.title + " » ?", 24.0f, Weight::ExtraBold, theme::Ink, width);
        space(2.0f);
        textWrapped("Le jeu disparaît de la bibliothèque. Ses sauvegardes sont gardées.", 14.0f, Weight::Medium,
                    theme::Muted, width);
        space(8.0f);

        // Option : supprimer aussi le fichier (et les pistes d'un .cue).
        const std::vector<std::filesystem::path> files = Library::filesOf(game.path);
        const ImVec2 rowPos = ImGui::GetCursorPos();
        toggle("##deleteFiles", &m_deleteFiles);
        ImGui::SetCursorPos(ImVec2(rowPos.x + px(58.0f), rowPos.y - px(2.0f)));
        ImGui::BeginGroup();
        text(files.size() > 1 ? "Supprimer aussi les " + std::to_string(files.size()) + " fichiers du jeu"
                              : std::string("Supprimer aussi le fichier du jeu"),
             15.0f, Weight::Bold, m_deleteFiles ? theme::Danger : theme::Ink);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        textWrapped(toUtf8(game.path), 12.0f, Weight::Medium, theme::Muted, width - px(58.0f));
        if (m_deleteFiles)
            textWrapped("Envoyé à la Corbeille de Windows : vous pourrez encore le récupérer.", 12.0f,
                        Weight::SemiBold, theme::Danger, width - px(58.0f));
        else
            textWrapped("Sinon, le fichier reste sur le disque ; Paramètres > Dossiers permet de réafficher le jeu.",
                        12.0f, Weight::Medium, theme::Muted, width - px(58.0f));
        ImGui::EndGroup();
        space(12.0f);

        const float buttonsW = px(110.0f + 10.0f + 170.0f);
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + width - buttonsW);
        if (button("Annuler##delete", {.style = ButtonStyle::Outline, .width = 110.0f}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        sameLine();
        if (button(m_deleteFiles ? "Supprimer##confirm" : "Retirer##confirm",
                   {.style = ButtonStyle::Danger, .icon = draw::Icon::Trash, .width = 170.0f})) {
            m_removeRequested = true; // exécuté à la fin de draw()
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void GameScreen::drawRename(UiContext& ctx, const Game& game)
{
    if (m_openRename) {
        ImGui::OpenPopup("##rename");
        m_openRename = false;
        m_focusRename = true;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(560.0f), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(30.0f, 30.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##rename", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float width = ImGui::GetContentRegionAvail().x;
        text("Renommer le jeu", 24.0f, Weight::ExtraBold);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        const std::string original = cleanTitle(toUtf8(game.path.stem()));
        text("Nom d'origine : " + original, 14.0f, Weight::Medium, theme::Muted);
        space(6.0f);
        if (m_focusRename) {
            ImGui::SetKeyboardFocusHere();
            m_focusRename = false;
        }
        textField("##name", m_renameBuffer, sizeof(m_renameBuffer), "Nom du jeu", width / theme::scale());
        // Entrée valide directement.
        const bool enter = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        space(10.0f);

        GameRecord& record = ctx.userData.record(game.path);
        auto apply = [&](std::string name) {
            // Une tabulation casserait le fichier userdata.txt.
            std::erase(name, '\t');
            record.title = (name == original) ? std::string() : name;
            ctx.userData.save();
            ctx.rescan();
            ctx.notify("Jeu renommé");
            ImGui::CloseCurrentPopup();
        };
        // À gauche : revenir au nom d'origine (si le jeu a été renommé).
        // À droite : Annuler et Enregistrer.
        const float rowY = ImGui::GetCursorPosY();
        if (!record.title.empty() && button("Revenir au nom d'origine", {.style = ButtonStyle::Ghost}))
            apply(original);
        const float buttonsW = px(110.0f + 10.0f + 130.0f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x + width - buttonsW, rowY));
        if (button("Annuler##rename", {.style = ButtonStyle::Outline, .width = 110.0f}))
            ImGui::CloseCurrentPopup();
        sameLine();
        const bool valid = m_renameBuffer[0] != '\0';
        if ((button("Enregistrer", {.style = ButtonStyle::Primary, .width = 130.0f, .disabled = !valid}) || enter) && valid)
            apply(m_renameBuffer);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void GameScreen::drawOnlineCovers(UiContext& ctx, const Game& game, float width)
{
    scrape::Scraper& scraper = ctx.scraper;
    // Première recherche : le nom officiel du jeu, ou son titre.
    if (!m_coverSearchStarted) {
        m_coverSearchStarted = true;
        const scrape::GameInfo* info = scraper.find(game.path);
        const std::string query = info && !info->name.empty() ? cleanTitle(info->name) : game.title;
        std::snprintf(m_coverQuery, sizeof(m_coverQuery), "%s", query.c_str());
        scraper.searchCovers(game, m_coverQuery);
    }
    const float buttonW = 140.0f;
    searchField("##coverQuery", m_coverQuery, sizeof(m_coverQuery), "Nom du jeu (en anglais)",
                (width - px(buttonW + 10.0f)) / theme::scale());
    const bool enter = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    sameLine();
    if ((button("Rechercher##covers", {.style = ButtonStyle::Primary, .icon = draw::Icon::Search, .width = buttonW}) || enter) &&
        m_coverQuery[0] != '\0')
        scraper.searchCovers(game, m_coverQuery);
    space(8.0f);

    // Les jaquettes trouvées : 6 par ligne, cliquer pour choisir.
    const std::vector<scrape::CoverChoice>& choices = scraper.coverChoices();
    GameRecord& record = ctx.userData.record(game.path);
    const float gap = px(14.0f);
    const int perRow = 6;
    const float coverW = (width - px(8.0f) - gap * (perRow - 1)) / perRow;
    const float coverH = coverW * 1.36f;
    const float labelH = px(30.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    beginCard("##onlineCovers", ImVec2(width, (coverH + labelH) * 2 + gap + px(8.0f)), 4.0f, theme::Surface, 0.0f, true);
    dl = ImGui::GetWindowDrawList(); // la carte est une fenêtre à part (elle défile)
    for (size_t i = 0; i < choices.size(); ++i) {
        const scrape::CoverChoice& c = choices[i];
        if (i % perRow != 0)
            ImGui::SameLine(0.0f, gap);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##choice", ImVec2(coverW, coverH + labelH), ImGuiButtonFlags_EnableNav);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", c.name.c_str());
        const bool selected = record.cover == c.file;
        const ImVec2 max(p.x + coverW, p.y + coverH);
        if (!ctx.covers.drawImage(dl, p, max, c.file, px(12.0f)))
            draw::rect(dl, p, max, theme::Placeholder, px(12.0f));
        if (selected || hovered)
            dl->AddRect(ImVec2(p.x - px(2.0f), p.y - px(2.0f)), ImVec2(max.x + px(2.0f), max.y + px(2.0f)),
                        selected ? theme::Accent : theme::Hairline, px(13.0f), 0, px(selected ? 3.0f : 2.0f));
        // Sous l'image : la région et la version (entre parenthèses dans le nom).
        const size_t paren = c.name.find(" (");
        const std::string label = paren == std::string::npos ? c.name : c.name.substr(paren + 1);
        draw::textEllipsis(dl, ImVec2(p.x, max.y + px(7.0f)), Weight::SemiBold, 12.0f, selected ? theme::Accent : theme::Muted,
                           label, coverW);
        if (clicked) {
            record.cover = c.file;
            ctx.userData.save();
            ctx.notify("Jaquette changée");
        }
        ImGui::PopID();
    }
    if (choices.empty()) {
        space(40.0f);
        const char* message = scraper.coverSearchBusy() ? "Recherche des jaquettes…"
                                                        : "Aucune jaquette trouvée : essayez un autre nom, en anglais.";
        const float tw = draw::measure(Weight::SemiBold, 14.0f, message).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) / 2);
        text(message, 14.0f, Weight::SemiBold, theme::Muted);
    } else if (scraper.coverSearchBusy()) {
        text("Recherche d'autres jaquettes…", 13.0f, Weight::SemiBold, theme::Muted);
    }
    endCard();
    if (!record.cover.empty()) {
        space(4.0f);
        if (button("Revenir à la jaquette automatique", {.style = ButtonStyle::Ghost, .height = 34.0f, .fontSize = 13.0f})) {
            record.cover.clear();
            ctx.userData.save();
        }
    }
}

void GameScreen::drawCoverPicker(UiContext& ctx, const Game& game)
{
    if (ctx.nav.openCoverPicker) {
        ctx.nav.openCoverPicker = false;
        m_openPicker = true;
    }
    if (m_openPicker) {
        ImGui::OpenPopup("##coverPicker");
        m_openPicker = false;
        m_coverSearchStarted = false; // nouvelle recherche à chaque ouverture
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(780.0f), 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(30.0f, 30.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##coverPicker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 top = ImGui::GetCursorPos();
        text("Changer la jaquette", 24.0f, Weight::ExtraBold);
        text(game.title + " · " + game.system->name, 14.0f, Weight::Medium, theme::Muted);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(top.x + width - px(40.0f), top.y));
        if (iconButton("##close", draw::Icon::Close, ButtonStyle::Soft, 40.0f))
            ImGui::CloseCurrentPopup();
        ImGui::SetCursorPos(after);
        space(8.0f);

        static const char* const tabs[] = {"Trouvées en ligne", "Depuis un fichier"};
        segmented("##tabs", tabs, &m_pickerTab);
        space(10.0f);

        GameRecord& record = ctx.userData.record(game.path);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float coverW = (width - px(16.0f) * 3) / 4;
        const float coverH = coverW * 1.36f;
        if (m_pickerTab == 0) {
            drawOnlineCovers(ctx, game, width);
        } else {
            // Aperçu : jaquette actuelle + jaquette générée.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ctx.covers.draw(dl, p, ImVec2(p.x + coverW, p.y + coverH), game, ctx.userData, px(12.0f), 18.0f);
            ImGui::Dummy(ImVec2(coverW, coverH));
            ImGui::SameLine(0.0f, px(24.0f));
            ImGui::BeginGroup();
            text(record.cover.empty() ? "Jaquette générée" : "Jaquette personnalisée", 16.0f, Weight::ExtraBold);
            textWrapped("Choisissez une image PNG ou JPG sur votre ordinateur. Elle sera copiée dans le dossier covers "
                        "de l'émulateur.",
                        14.0f, Weight::Medium, theme::Muted, width - coverW - px(24.0f));
            space(6.0f);
            if (button("Choisir une image", {.style = ButtonStyle::Primary, .icon = draw::Icon::Image}))
                ctx.openDialog(DialogPurpose::ChooseCover);
            if (!record.cover.empty()) {
                sameLine();
                if (button("Revenir à la jaquette générée", {.style = ButtonStyle::Outline})) {
                    record.cover.clear();
                    ctx.userData.save();
                }
            }
            ImGui::EndGroup();
        }
        space(10.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - px(110.0f));
        if (button("Terminé##close", {.style = ButtonStyle::Primary, .width = 110.0f}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

// --- Sauvegarde du jeu ----------------------------------------------------------------

namespace {

// Les fenêtres de la fiche ont toutes le même habillage.
bool beginModal(const char* id, float width, float height = 0.0f)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(width), height > 0.0f ? px(height) : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(30.0f, 30.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | (height > 0.0f ? 0 : ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::BeginPopupModal(id, nullptr, flags))
        return true;
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return false;
}

void endModal()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

// Titre + bouton fermer en haut à droite. Renvoie true si on ferme.
bool modalHeader(const char* title, const std::string& subtitle, float width)
{
    const ImVec2 top = ImGui::GetCursorPos();
    text(title, 24.0f, Weight::ExtraBold);
    text(subtitle, 14.0f, Weight::Medium, theme::Muted);
    const ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(top.x + width - px(40.0f), top.y));
    const bool close = iconButton("##close", draw::Icon::Close, ButtonStyle::Soft, 40.0f);
    ImGui::SetCursorPos(after);
    return close;
}

} // namespace

void GameScreen::drawSaveManager(UiContext& ctx, const Game& game)
{
    if (ctx.nav.openSave) {
        ctx.nav.openSave = false;
        m_openSave = true;
    }
    if (m_openSave) {
        ImGui::OpenPopup("##gameSave");
        m_openSave = false;
    }
    if (!beginModal("##gameSave", 620.0f))
        return;
    const float width = ImGui::GetContentRegionAvail().x;
    if (modalHeader("Sauvegarde du jeu", game.title + " · " + game.system->name, width))
        ImGui::CloseCurrentPopup();
    space(6.0f);
    textWrapped("Votre progression, enregistrée quand vous sauvegardez DANS le jeu (comme la pile d'une vraie "
                "cartouche ou une carte mémoire). Elle est chargée à chaque lancement avec « Jouer ».",
                14.0f, Weight::Medium, theme::Body, width);
    space(8.0f);

    // Le fichier : où il est, sa date, sa taille.
    const fs::path file = gameSaveFile(ctx, game);
    const long long time = fileTime(file);
    std::error_code ec;
    beginCard("##saveFile", ImVec2(width, px(96.0f)), 16.0f, theme::Ground, 16.0f);
    const float inner = ImGui::GetContentRegionAvail().x;
    if (time > 0) {
        text("Modifiée " + formatAgo(time) + "  ·  " + formatSize(fs::file_size(file, ec)), 15.0f, Weight::Bold);
    } else {
        text("Pas encore de sauvegarde", 15.0f, Weight::Bold);
        textWrapped("Elle sera créée la première fois que vous sauvegarderez dans le jeu.", 13.0f, Weight::Medium,
                    theme::Muted, inner);
    }
    textWrapped(toUtf8(file), 12.0f, Weight::Medium, theme::Muted, inner);
    endCard();
    space(8.0f);

    // Les actions.
    if (button("Remplacer par un fichier…", {.style = ButtonStyle::Primary, .icon = draw::Icon::Upload}))
        ctx.openDialog(DialogPurpose::ImportSave); // la suite : Shell::onDialogResult
    sameLine();
    if (button("Ouvrir le dossier", {.style = ButtonStyle::Outline, .icon = draw::Icon::Folder}))
        openFolder(gameSaveDir(ctx, game));
    if (time > 0) {
        sameLine();
        if (button("Supprimer##save", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Trash})) {
            std::string error;
            ctx.notify(moveToTrash({file}, error) ? "Sauvegarde envoyée à la Corbeille"
                                                  : "Impossible de supprimer la sauvegarde : " + error);
        }
    }
    space(8.0f);
    textWrapped("Les fichiers .srm et .sav des autres émulateurs (RetroArch, mGBA, VisualBoyAdvance, Snes9x...) "
                "marchent aussi. L'ancienne sauvegarde part à la Corbeille de Windows : on peut la récupérer.",
                12.0f, Weight::Medium, theme::Muted, width);
    space(2.0f);
    textWrapped("Attention : une sauvegarde d'état (automatique ou F2) contient l'ancienne progression. Après un "
                "remplacement, lancez le jeu avec « Jouer ».",
                12.0f, Weight::SemiBold, theme::Warning, width);
    endModal();
}

// --- Codes de triche ------------------------------------------------------------------

std::vector<Cheat>& GameScreen::cheatList(UiContext& ctx, const Game& game)
{
    // Relu seulement quand on change de jeu (pas à chaque image).
    if (m_cheatsOf != game.path) {
        m_cheatsOf = game.path;
        m_cheats = loadCheats(cheatsFile(ctx, game));
    }
    return m_cheats;
}

void GameScreen::drawCheats(UiContext& ctx, const Game& game)
{
    if (ctx.nav.openCheats) {
        ctx.nav.openCheats = false;
        m_openCheats = true;
        m_cheatTab = 1;
    }
    if (m_openCheats) {
        ImGui::OpenPopup("##cheats");
        m_openCheats = false;
        m_cheatSearchStarted = false;
        m_cheatFilter[0] = '\0';
    }
    if (!beginModal("##cheats", 820.0f, 680.0f))
        return;
    const float width = ImGui::GetContentRegionAvail().x;
    if (modalHeader("Codes de triche", game.title + " · " + game.system->name, width))
        ImGui::CloseCurrentPopup();
    space(6.0f);
    std::vector<Cheat>& cheats = cheatList(ctx, game);
    bool changed = false;
    static const char* const tabs[] = {"Mes codes", "Trouvés en ligne"};
    segmented("##cheatTabs", tabs, &m_cheatTab);
    space(8.0f);

    if (m_cheatTab == 0) {
        // --- Mes codes : activer, désactiver, supprimer.
        const float listH = ImGui::GetContentRegionAvail().y - px(150.0f);
        ImGui::BeginChild("##myCheats", ImVec2(width, listH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowW = ImGui::GetContentRegionAvail().x;
        int remove = -1;
        for (size_t i = 0; i < cheats.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            draw::rect(dl, p, ImVec2(p.x + rowW, p.y + px(58.0f)), cheats[i].enabled ? theme::AccentSoft : theme::Ground,
                       px(14.0f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(14.0f), p.y + px(14.0f)));
            changed |= toggle("##on", &cheats[i].enabled);
            draw::textEllipsis(dl, ImVec2(p.x + px(74.0f), p.y + px(10.0f)), Weight::Bold, 14.0f, theme::Ink,
                               cheats[i].name, rowW - px(140.0f));
            draw::textEllipsis(dl, ImVec2(p.x + px(74.0f), p.y + px(31.0f)), Weight::Medium, 12.0f, theme::Muted,
                               cheats[i].code, rowW - px(140.0f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + rowW - px(50.0f), p.y + px(11.0f)));
            if (iconButton("##remove", draw::Icon::Trash, ButtonStyle::Ghost, 36.0f, "Supprimer ce code"))
                remove = static_cast<int>(i);
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(rowW, px(58.0f)));
            ImGui::PopID();
        }
        if (cheats.empty())
            textWrapped("Aucun code pour ce jeu. Ajoutez-en un ci-dessous, ou piochez dans « Trouvés en ligne ».", 14.0f,
                        Weight::Medium, theme::Muted, rowW);
        ImGui::EndChild();
        if (remove >= 0) {
            cheats.erase(cheats.begin() + remove);
            changed = true;
        }

        // Ajouter un code à la main.
        space(6.0f);
        textField("##cheatName", m_cheatName, sizeof(m_cheatName), "Nom (ex. Vies infinies)", 250.0f);
        sameLine();
        textField("##cheatCode", m_cheatCode, sizeof(m_cheatCode), "Code (ex. 7E0DBF63)",
                  (width / theme::scale()) - 250.0f - 130.0f - 20.0f);
        sameLine();
        if (button("Ajouter##cheat", {.style = ButtonStyle::Primary, .icon = draw::Icon::Plus, .width = 120.0f,
                                      .disabled = m_cheatCode[0] == '\0'})) {
            cheats.push_back({true, m_cheatName[0] ? m_cheatName : m_cheatCode, m_cheatCode});
            m_cheatName[0] = m_cheatCode[0] = '\0';
            changed = true;
        }
        space(4.0f);
        textWrapped("Codes Game Genie, Action Replay, GameShark... selon la console. Plusieurs codes pour une même "
                    "triche : séparez-les par « + ». Appliqués au prochain lancement du jeu ; jamais pendant une partie "
                    "en ligne sur une console partagée.",
                    12.0f, Weight::Medium, theme::Muted, width);
    } else {
        // --- Trouvés en ligne : la base libretro (des milliers de codes).
        scrape::Scraper& scraper = ctx.scraper;
        if (!m_cheatSearchStarted) {
            m_cheatSearchStarted = true;
            scraper.searchCheats(game);
        }
        searchField("##cheatFilter", m_cheatFilter, sizeof(m_cheatFilter), "Filtrer (ex. vie, argent, infini)",
                    width / theme::scale());
        space(4.0f);
        text(scraper.cheatSearchBusy() ? std::string("Recherche des codes…") : scraper.cheatSearchNote(), 13.0f,
             Weight::SemiBold, theme::Muted);
        space(4.0f);
        ImGui::BeginChild("##onlineCheats", ImVec2(width, ImGui::GetContentRegionAvail().y));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowW = ImGui::GetContentRegionAvail().x;
        std::string needle = m_cheatFilter;
        for (char& c : needle)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::vector<Cheat>& found = scraper.onlineCheats();
        for (size_t i = 0; i < found.size(); ++i) {
            const Cheat& c = found[i];
            if (!needle.empty()) {
                std::string name = c.name;
                for (char& ch : name)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (name.find(needle) == std::string::npos)
                    continue;
            }
            const bool already = std::any_of(cheats.begin(), cheats.end(), [&](const Cheat& m) { return m.code == c.code; });
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            draw::rect(dl, p, ImVec2(p.x + rowW, p.y + px(54.0f)), theme::Ground, px(14.0f));
            draw::textEllipsis(dl, ImVec2(p.x + px(16.0f), p.y + px(9.0f)), Weight::Bold, 14.0f, theme::Ink, c.name,
                               rowW - px(160.0f));
            draw::textEllipsis(dl, ImVec2(p.x + px(16.0f), p.y + px(29.0f)), Weight::Medium, 12.0f, theme::Muted, c.code,
                               rowW - px(160.0f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + rowW - px(122.0f), p.y + px(10.0f)));
            if (button(already ? "Ajouté##add" : "Ajouter##add",
                       {.style = already ? ButtonStyle::Soft : ButtonStyle::Outline, .height = 34.0f, .width = 110.0f,
                        .fontSize = 13.0f, .disabled = already})) {
                cheats.push_back({true, c.name, c.code});
                changed = true;
                ctx.notify("Code ajouté et activé : " + c.name);
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(rowW, px(54.0f)));
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    if (changed)
        saveCheats(cheatsFile(ctx, game), cheats);
    endModal();
}

} // namespace ui
