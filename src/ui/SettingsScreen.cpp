// Paramètres : onglets à gauche, réglages de l'onglet choisi à droite.
// Chaque modification est enregistrée et appliquée tout de suite.

#include "../CoreOptions.hpp"
#include "../ExternalSetup.hpp"
#include "../Lang.hpp"
#include "../UiSounds.hpp"
#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Themes.hpp"
#include "Widgets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

using theme::px;
using theme::Weight;

namespace ui {

namespace {

// Une ligne de réglage : libellé et explication à gauche, contrôle à droite.
// Le contrôle est dessiné par la lambda "control", alignée à droite.
template <typename Control>
bool settingRow(const char* label, const char* hint, float width, float controlWidth, Control&& control)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(p, ImVec2(p.x + width, p.y), theme::Divider, px(1.0f));
    const float rowH = px(76.0f);
    draw::text(dl, ImVec2(p.x, p.y + px(17.0f)), Weight::Bold, 15.0f, theme::Ink, label);
    draw::textEllipsis(dl, ImVec2(p.x, p.y + px(40.0f)), Weight::Medium, 13.0f, theme::Muted, hint,
                       width - controlWidth - px(24.0f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - controlWidth, p.y + px(18.0f)));
    ImGui::PushID(label);
    const bool changed = control();
    ImGui::PopID();
    // ImGui exige un élément pour "réserver" la place de la ligne : un
    // simple déplacement du curseur ne suffit pas à agrandir la fenêtre.
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, rowH - ImGui::GetStyle().ItemSpacing.y));
    return changed;
}

float segmentedWidth(std::initializer_list<const char*> labels)
{
    float w = px(6.0f);
    for (const char* l : labels)
        w += draw::measure(Weight::Bold, 13.0f, l).x + px(28.0f);
    return w;
}

} // namespace

void SettingsScreen::drawControls(UiContext& ctx, float width)
{
    Input& input = ctx.input;
    const std::vector<Input::DeviceInfo> devices = input.devices();
    if (m_device >= static_cast<int>(devices.size()))
        m_device = -1;

    // --- Choix de l'appareil : le clavier, puis chaque manette.
    if (chip("##kbd", "Clavier", "J1", theme::Ink, m_device == -1)) {
        m_device = -1;
        input.cancelCapture();
    }
    for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
        const Input::DeviceInfo& d = devices[i];
        std::string name = d.name.size() > 28 ? d.name.substr(0, 26) + "…" : d.name;
        const std::string player = "J" + std::to_string(d.player);
        ImGui::SameLine(0.0f, px(8.0f));
        if (ImGui::GetCursorPosX() + chipWidth(name.c_str(), player.c_str()) > width) {
            ImGui::NewLine();
        }
        ImGui::PushID(i);
        // Pastille verte : manette reconnue ; orange : manette générique.
        if (chip("##pad", name.c_str(), player.c_str(), d.known ? theme::Success : theme::rgb(0xC2560C), m_device == i)) {
            m_device = i;
            input.cancelCapture();
        }
        ImGui::PopID();
    }
    if (devices.empty()) {
        space(2.0f);
        text("Aucune manette branchée : branchez-en une, elle apparaîtra ici.", 13.0f, Weight::Medium, theme::Muted);
    } else if (m_device >= 0 && !devices[m_device].known) {
        space(2.0f);
        pill("Manette générique : vérifiez ses touches ci-dessous", Tone::Warning);
    }
    space(8.0f);

    // --- Manette choisie : dispositions toutes prêtes (stick arcade...).
    if (m_device >= 0) {
        text("Disposition", 13.0f, Weight::Bold, theme::Muted);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
        if (button("Manette classique", {.style = ButtonStyle::Outline, .icon = draw::Icon::Gamepad, .height = 38.0f,
                                         .fontSize = 13.0f})) {
            input.applyLayout(m_device, Input::Layout::Pad);
            ctx.notify("Disposition « manette classique » appliquée");
        }
        sameLine();
        if (button("Stick arcade", {.style = ButtonStyle::Outline, .icon = draw::Icon::Gamepad, .height = 38.0f,
                                    .fontSize = 13.0f,
                                    .tooltip = "2 rangées de 4 boutons : Y X L L2 en haut, B A R R2 en bas"})) {
            input.applyLayout(m_device, Input::Layout::Arcade);
            ctx.notify("Disposition « stick arcade » appliquée");
        }
        sameLine(20.0f);
        bool stickDpad = input.stickAsDpad(m_device);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5.0f));
        if (toggle("##stickDpad", &stickDpad))
            input.setStickAsDpad(m_device, stickDpad);
        sameLine(8.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4.0f));
        text("Le stick pilote aussi la croix", 13.0f, Weight::SemiBold, theme::Body);
        space(2.0f);
        text("Stick arcade : haut Y X L L2, bas B A R R2 (Street Fighter, Neo Geo...). Tout reste modifiable ci-dessous.",
             12.0f, Weight::Medium, theme::Muted);
        space(8.0f);
    }

    // --- Les commandes, sur deux colonnes, dans l'ordre d'une manette.
    static const int kOrder[] = {4, 5, 6, 7, 0, 8, 1, 9, 10, 11, 12, 13, 14, 15, 2, 3, 16, 17, 18, 19};
    const float gap = px(12.0f);
    const float cellW = (width - gap) / 2;
    const float cellH = px(58.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int column = 0;
    for (int binding : kOrder) {
        if (m_device == -1 && binding >= Input::kFirstAnalog)
            continue; // pas de stick analogique au clavier
        if (column == 1)
            ImGui::SameLine(0.0f, gap);
        ImGui::PushID(binding);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cellW, cellH));
        const bool capturing = input.isCapturing(m_device, binding);
        draw::rect(dl, p, ImVec2(p.x + cellW, p.y + cellH), capturing ? theme::AccentSoft : theme::Ground, px(14.0f),
                   capturing ? theme::Accent : 0);
        const float buttonW = px(capturing ? 96.0f : 92.0f);
        const float textW = cellW - buttonW - px(32.0f);
        draw::textEllipsis(dl, ImVec2(p.x + px(16.0f), p.y + px(10.0f)), Weight::Bold, 14.0f, theme::Ink,
                           Input::bindingName(binding), textW);
        draw::textEllipsis(dl, ImVec2(p.x + px(16.0f), p.y + px(31.0f)), Weight::SemiBold, 13.0f,
                           capturing ? theme::Accent : theme::Muted,
                           capturing ? (binding >= Input::kFirstAnalog ? "Bougez le stick… (Échap : annuler)"
                                                                       : "Appuyez sur une touche… (Échap : annuler)")
                                     : input.bindingLabel(m_device, binding),
                           textW);
        ImGui::SetCursorScreenPos(ImVec2(p.x + cellW - buttonW - px(12.0f), p.y + (cellH - px(34.0f)) / 2));
        if (capturing) {
            if (button("Annuler", {.style = ButtonStyle::Outline, .height = 34.0f, .width = 96.0f, .fontSize = 13.0f}))
                input.cancelCapture();
        } else if (button("Modifier", {.style = ButtonStyle::Outline, .height = 34.0f, .width = 92.0f, .fontSize = 13.0f})) {
            // Le bouton de manette qui vient d'être assigné peut aussi
            // "cliquer" via la navigation ImGui : on ignore ce clic-là.
            if (SDL_GetTicks() - input.lastCaptureTime() > 400)
                input.startCapture(m_device, binding);
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(cellW, cellH));
        ImGui::PopID();
        column = 1 - column;
        if (column == 0)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + gap);
    }
    if (column == 1)
        ImGui::NewLine();
    space(4.0f);
    if (button("Réinitialiser les touches", {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 40.0f}))
        input.resetBindings(m_device);
}

void SettingsScreen::drawExternal(UiContext& ctx, float width)
{
    // Pour chaque console : l'émulateur (installé en un clic depuis sa page
    // officielle, ou choisi à la main), puis ses fichiers système.
    bool changed = false;
    const std::string installing = ctx.installer.busySystem();
    for (const System& s : Library::systems()) {
        if (!s.external())
            continue;
        const std::filesystem::path exe = ctx.library.externalPath(s);
        const bool installable = ExternalInstaller::sourceFor(s.id) != nullptr;
        const bool mine = installing == s.id;
        const std::string label = std::string(s.name) + "  ·  " + s.externalName;
        std::string hint;
        if (mine) {
            const float p = ctx.installer.progress();
            hint = ctx.installer.stepText() + (p >= 0.0f ? " " + std::to_string(static_cast<int>(p * 100)) + " %" : "");
        } else if (!exe.empty()) {
            hint = toUtf8(exe);
        } else {
            hint = installable ? "Pas encore installé" : std::string("Introuvable : choisissez ") + s.externalExe;
        }
        ImGui::PushID(s.id);
        // Les arguments ({rom} = le jeu), modifiables pour les cas particuliers.
        std::string& args = m_argsEdit[s.id];
        if (args.empty()) {
            const auto it = ctx.settings.externalArgs.find(s.id);
            args = it != ctx.settings.externalArgs.end() && !it->second.empty() ? it->second : s.externalArgs;
        }
        settingRow(label.c_str(), hint.c_str(), width, px(150.0f + 10.0f + 130.0f + 10.0f + 110.0f), [&] {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%s", args.c_str());
            if (textField("##args", buffer, sizeof(buffer), "{rom}", 150.0f)) {
                args = buffer;
                ctx.settings.externalArgs[s.id] = args == s.externalArgs ? std::string() : args;
                changed = true;
            }
            ImGui::SameLine(0.0f, px(10.0f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(2.0f));
            if (installable) {
                // Télécharge la dernière version sur la page GitHub officielle.
                const char* action = mine ? "Installation…" : exe.empty() ? "Installer" : "Mettre à jour";
                if (button(action, {.style = exe.empty() ? ButtonStyle::Accent : ButtonStyle::Outline, .height = 38.0f,
                                    .width = 130.0f, .fontSize = 13.0f, .disabled = !installing.empty()}))
                    ctx.installer.start(s.id);
            } else {
                ImGui::Dummy(ImVec2(px(130.0f), px(38.0f)));
            }
            ImGui::SameLine(0.0f, px(10.0f));
            if (button("Choisir…", {.style = exe.empty() && !installable ? ButtonStyle::Primary : ButtonStyle::Outline,
                                     .height = 38.0f, .width = 110.0f, .fontSize = 13.0f})) {
                ctx.nav.externalChoosing = s.id;
                ctx.openDialog(DialogPurpose::ChooseExternal);
            }
            return false;
        });

        // Les fichiers système de la console (firmware, clés, BIOS).
        const external::SystemFiles files = external::systemFiles(s.id, exe, ctx.dir("bios"));
        if (files.needed) {
            // Décalés vers la droite (sous leur console) : on traduit chaque
            // morceau avant d'ajouter les espaces, sinon la traduction ne les
            // reconnaîtrait pas.
            const std::string indent = "      ";
            const std::string subLabel = indent + std::string(lang::tr(files.label));
            const std::string subHint = indent + std::string(lang::tr(files.status)) + " · " + std::string(lang::tr(files.help));
            settingRow(subLabel.c_str(), subHint.c_str(), width, px(130.0f + 10.0f + 110.0f), [&] {
                if (files.officialPage) {
                    if (button(files.officialLabel, {.style = ButtonStyle::Soft, .height = 38.0f, .width = 130.0f, .fontSize = 13.0f}))
                        SDL_OpenURL(files.officialPage);
                } else {
                    ImGui::Dummy(ImVec2(px(130.0f), px(38.0f)));
                }
                ImGui::SameLine(0.0f, px(10.0f));
                if (button("Importer…", {.style = files.ready ? ButtonStyle::Outline : ButtonStyle::Primary, .height = 38.0f,
                                         .width = 110.0f, .fontSize = 13.0f})) {
                    ctx.nav.externalChoosing = s.id;
                    ctx.openDialog(DialogPurpose::ImportSystemFiles);
                }
                return false;
            });
        }
        ImGui::PopID();
    }
    if (changed)
        ctx.settingsChanged();
    space(6.0f);
    textWrapped("« Installer » télécharge l'émulateur sur sa page GitHub officielle, dans le dossier emulators. Les "
                "fichiers système (firmware, clés, BIOS) ne sont jamais téléchargés à votre place : ils viennent de "
                "votre console, ou du site officiel de Sony pour le firmware PS3. Pas d'installation automatique pour "
                "la Switch : choisissez vous-même votre émulateur. Pas de jeu en ligne ni de codes de triche avec ces "
                "émulateurs. Les arguments : {rom} est remplacé par le jeu.",
                13.0f, Weight::Medium, theme::Muted, width);
    space(4.0f);
    text("Sites officiels :", 13.0f, Weight::SemiBold, theme::Muted);
    static const std::pair<const char*, const char*> sites[] = {
        {"xemu (Xbox)", "https://xemu.app"}, {"Xenia (Xbox 360)", "https://xenia.jp"},
        {"RPCS3 (PS3)", "https://rpcs3.net"}, {"shadPS4 (PS4)", "https://shadps4.net"}};
    for (size_t i = 0; i < std::size(sites); ++i) {
        if (i > 0)
            sameLine(16.0f);
        if (link(sites[i].first, 13.0f))
            SDL_OpenURL(sites[i].second);
    }
}

void SettingsScreen::drawCores(UiContext& ctx, float width)
{
    namespace fs = std::filesystem;
    // La liste des cores : celui de chaque console, puis ses cores de secours
    // (MAME pour l'arcade). Un core qui sert à plusieurs consoles (Dolphin :
    // GameCube et Wii) n'apparaît qu'une fois.
    struct CoreRow {
        std::string file;
        std::string consoles;
        bool backup = false; // core de secours (souvent très gros : MAME)
    };
    std::vector<CoreRow> rows;
    auto addRow = [&](const std::string& file, const char* console, bool backup) {
        for (CoreRow& r : rows)
            if (r.file == file) {
                if (r.consoles.find(console) == std::string::npos)
                    r.consoles += std::string(" · ") + console;
                return;
            }
        rows.push_back({file, console, backup});
    };
    for (const System& s : Library::systems()) {
        if (s.external() || !s.coreFile || !*s.coreFile)
            continue;
        addRow(s.coreFile, s.name, false);
        if (s.otherCores) {
            std::istringstream others(s.otherCores);
            std::string other;
            while (others >> other)
                addRow(other, s.name, true);
        }
    }

    const fs::path dir = ctx.library.coresDir();
    std::error_code ec;
    auto installed = [&](const CoreRow& r) { return fs::is_regular_file(dir / r.file, ec); };
    const std::string busy = ctx.installer.busySystem();
    const bool working = !busy.empty() || !m_coreQueue.empty();

    // Les deux actions groupées.
    // (Les cores de secours ne comptent pas comme "manquants" : MAME fait 350 Mo.)
    int missing = 0, present = 0;
    for (const CoreRow& r : rows) {
        if (installed(r))
            ++present;
        else if (!r.backup)
            ++missing;
    }
    // Mise à jour automatique : au démarrage, au plus une fois par jour.
    {
        CoreUpdater& updater = ctx.coreUpdater;
        std::string hint = "Au démarrage, une fois par jour, sans toucher au core du jeu en cours";
        if (updater.checking())
            hint = "Recherche des mises à jour…";
        else if (updater.queued() > 0)
            hint = "Mise à jour en cours · encore " + std::to_string(updater.queued());
        else if (updater.lastCheck() > 0)
            hint = "Dernière vérification : " + formatAgo(updater.lastCheck()) +
                   (updater.outdated().empty() ? " · tout est à jour"
                                               : " · " + std::to_string(updater.outdated().size()) + " mise(s) à jour disponible(s)");
        bool autoUpdate = ctx.settings.coreAutoUpdate;
        settingRow("Mise à jour automatique", hint.c_str(), width, px(46.0f + 12.0f + 190.0f), [&] {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (button("Vérifier maintenant", {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 38.0f,
                                               .width = 190.0f, .fontSize = 13.0f, .disabled = updater.checking()}))
                updater.check(true);
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(202.0f), p.y + px(4.0f)));
            if (toggle("##coreAuto", &autoUpdate)) {
                ctx.settings.coreAutoUpdate = autoUpdate;
                ctx.settingsChanged();
            }
            return false;
        });
        space(6.0f);
    }
    if (button("Installer les cores manquants", {.style = ButtonStyle::Accent, .icon = draw::Icon::Plus, .height = 42.0f,
                                                 .fontSize = 14.0f, .disabled = working || missing == 0})) {
        for (const CoreRow& r : rows)
            if (!r.backup && !installed(r))
                m_coreQueue.push_back(r.file);
    }
    sameLine();
    if (button("Tout mettre à jour", {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 42.0f,
                                      .fontSize = 14.0f, .disabled = working || present == 0})) {
        for (const CoreRow& r : rows)
            if (installed(r))
                m_coreQueue.push_back(r.file);
    }
    if (working) {
        sameLine(16.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(11.0f));
        const std::string queue = m_coreQueue.empty() ? "" : " · encore " + std::to_string(m_coreQueue.size());
        text("Installation en cours" + queue, 13.0f, Weight::SemiBold, theme::Muted);
    }
    space(6.0f);

    for (const CoreRow& r : rows) {
        ImGui::PushID(r.file.c_str());
        const bool has = installed(r);
        const bool mine = busy == "core:" + r.file;
        std::string name = r.file.substr(0, r.file.find("_libretro"));
        const std::string label = name + "  ·  " + r.consoles + (r.backup ? "  (secours)" : "");
        std::string hint;
        if (mine) {
            const float p = ctx.installer.progress();
            hint = ctx.installer.stepText() + (p >= 0.0f ? " " + std::to_string(static_cast<int>(p * 100)) + " %" : "");
        } else if (has) {
            const auto size = fs::file_size(dir / r.file, ec);
            const auto time = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(dir / r.file, ec));
            const long long when = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
            char mo[32];
            std::snprintf(mo, sizeof(mo), "%.1f Mo", static_cast<double>(size) / (1024.0 * 1024.0));
            hint = std::string("Installé · ") + mo + " · " + formatAgo(when);
            if (ctx.coreUpdater.outdated().count(r.file))
                hint += " · nouvelle version disponible";
        } else {
            hint = r.backup ? "Pas installé · seulement si le premier core ne connaît pas le jeu" : "Pas installé";
        }
        settingRow(label.c_str(), hint.c_str(), width, px(150.0f), [&] {
            const char* action = mine ? "Installation…" : has ? "Mettre à jour" : "Installer";
            if (button(action, {.style = has ? ButtonStyle::Outline : ButtonStyle::Accent, .height = 38.0f, .width = 150.0f,
                                .fontSize = 13.0f, .disabled = working}))
                m_coreQueue.push_back(r.file);
            return false;
        });
        ImGui::PopID();
    }
    space(6.0f);
    textWrapped("Les cores viennent du serveur officiel de libretro (logiciels libres) et vont dans le dossier cores. "
                "« Mettre à jour » télécharge la dernière version. En ligne, vous et vos amis devez avoir exactement "
                "la même version : mettez à jour en même temps, ou envoyez-leur vos fichiers .dll.",
                13.0f, Weight::Medium, theme::Muted, width);
}

// --- Chat vocal ------------------------------------------------------------------

void SettingsScreen::drawVoice(UiContext& ctx, float width, bool& changed)
{
    Settings& s = ctx.settings;
    VoiceChat& voice = ctx.voice;
    space(10.0f);
    overline("CHAT VOCAL");
    static const char* const modes[] = {"Désactivé", "Micro ouvert", "Appuyer pour parler"};
    changed |= settingRow("Chat vocal", "Parler avec les membres du salon, même pendant la partie", width,
                          segmentedWidth({"Désactivé", "Micro ouvert", "Appuyer pour parler"}),
                          [&] { return segmented("##voiceMode", modes, &s.voiceMode); });
    if (s.voiceMode == 0) {
        voice.setTesting(false);
        return;
    }
    if (s.voiceMode == 2) {
        // La touche à maintenir : on clique, puis on appuie sur la touche voulue.
        if (m_waitingVoiceKey) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            for (int k = SDL_SCANCODE_A; k < SDL_SCANCODE_COUNT; ++k) {
                if (!keys[k])
                    continue;
                if (k != SDL_SCANCODE_ESCAPE) {
                    s.voiceKey = k;
                    changed = true;
                }
                m_waitingVoiceKey = false;
                break;
            }
        }
        const std::string keyName = m_waitingVoiceKey ? std::string("Appuyez sur une touche…")
                                                      : SDL_GetScancodeName(static_cast<SDL_Scancode>(s.voiceKey));
        settingRow("Touche pour parler", "Maintenue, elle ouvre votre micro (Échap pour annuler)", width, px(220.0f), [&] {
            if (button((keyName + "##voiceKey").c_str(), {.style = m_waitingVoiceKey ? ButtonStyle::Accent : ButtonStyle::Outline,
                                                         .height = 38.0f, .width = 220.0f, .fontSize = 13.0f}))
                m_waitingVoiceKey = true;
            return false;
        });
    } else {
        changed |= settingRow("Sensibilité du micro", "Plus haut : votre voix passe même si vous parlez doucement", width,
                              px(260.0f), [&] { return slider("##sens", &s.voiceSensitivity, 0, 100, 200.0f, " %"); });
    }
    changed |= settingRow("Volume des voix", "Le volume de vos amis (le jeu garde le sien)", width, px(260.0f),
                          [&] { return slider("##voiceVol", &s.voiceVolume, 0, 200, 200.0f, " %"); });

    // Tester son micro : on s'entend soi-même, et une jauge montre le volume
    // (le trait = le seuil à partir duquel la voix est envoyée).
    bool testing = voice.testing();
    const std::string hint = !voice.micError().empty() ? voice.micError()
                           : testing ? std::string("Vous vous entendez : parlez pour voir la jauge bouger")
                                     : std::string("Vous entendre vous-même pour régler le micro");
    settingRow("Tester mon micro", hint.c_str(), width, px(46.0f + 230.0f), [&] {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 bar(p.x, p.y + px(17.0f));
        const float barW = px(210.0f);
        draw::rect(dl, bar, ImVec2(bar.x + barW, bar.y + px(6.0f)), theme::Track, px(3.0f));
        if (testing) {
            const float level = voice.micLevel();
            const float gate = VoiceChat::threshold(s.voiceSensitivity);
            if (level > 0.02f)
                draw::rect(dl, bar, ImVec2(bar.x + barW * level, bar.y + px(6.0f)),
                           level >= gate || s.voiceMode == 2 ? theme::rgb(0x22C55E) : theme::Muted, px(3.0f));
            if (s.voiceMode == 1)
                dl->AddLine(ImVec2(bar.x + barW * gate, bar.y - px(4.0f)), ImVec2(bar.x + barW * gate, bar.y + px(12.0f)),
                            theme::Ink, px(2.0f));
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(230.0f), p.y));
        if (toggle("##micTest", &testing))
            voice.setTesting(testing);
        return false;
    });
}

// --- Discord ---------------------------------------------------------------------

void SettingsScreen::drawDiscord(UiContext& ctx, float width, bool& changed)
{
    Settings& s = ctx.settings;
    space(10.0f);
    overline("DISCORD");
    const std::string status = ctx.discord.status();
    changed |= settingRow("Afficher votre jeu sur Discord", ("« Joue à Zelda · Super Nintendo » · " + status).c_str(), width,
                          px(46.0f), [&] { return toggle("##discord", &s.discordPresence); });
    if (!s.discordPresence)
        return;
    // L'identifiant de l'application Discord : fourni avec l'émulateur, ou le vôtre.
    if (m_discordId[0] == '\0' && !s.discordAppId.empty())
        std::snprintf(m_discordId, sizeof(m_discordId), "%s", s.discordAppId.c_str());
    const bool builtIn = kDiscordAppId[0] != '\0';
    changed |= settingRow("Identifiant d'application", builtIn ? "Facultatif : celui de l'émulateur est déjà fourni"
                                                               : "Créez une application sur discord.com/developers et collez son « Application ID »",
                          width, px(240.0f), [&] {
                              if (!textField("##discordId", m_discordId, sizeof(m_discordId), builtIn ? "Celui de l'émulateur" : "Application ID",
                                             240.0f))
                                  return false;
                              s.discordAppId = m_discordId;
                              return true;
                          });
}

// --- Apparence ---------------------------------------------------------------------

namespace {

// Les couleurs proposées d'un clic dans le sélecteur (en plus de la roue).
constexpr unsigned kSwatches[] = {0x1F4FFF, 0x5865F2, 0x7C3AED, 0xEB459E, 0xE5484D, 0xF97316,
                                  0xF5B800, 0x22C55E, 0x0EA5E9, 0x00D1B2, 0x111114, 0xF4F4F1};

// Des dégradés tout prêts, façon Discord.
struct GradientPreset {
    unsigned from, to;
};
constexpr GradientPreset kGradients[] = {{0x5865F2, 0xEB459E}, {0x0F2027, 0x2C5364}, {0xFF7E5F, 0xFEB47B},
                                         {0x1A2980, 0x26D0CE}, {0x232526, 0x414345}, {0x3A1C71, 0xD76D77},
                                         {0x134E5E, 0x71B280}, {0x0B0C10, 0x1B2A4A}};

// La miniature d'un thème : son fond, une carte, des lignes de texte, un
// bouton d'accent et deux jaquettes.
void themePreview(ImDrawList* dl, const theme::ThemeInfo& t, ImVec2 min, ImVec2 max, float rounding)
{
    const theme::Palette& p = t.palette;
    if (t.background == theme::Background::Gradient)
        theme::gradientRect(dl, min, max, t.gradientFrom, t.gradientTo, t.gradientAngle, rounding);
    else
        draw::rect(dl, min, max, p.ground, rounding);
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    // La barre du haut : une pilule claire et le bouton d'accent.
    draw::rect(dl, ImVec2(min.x + w * 0.3f, min.y + h * 0.1f), ImVec2(min.x + w * 0.7f, min.y + h * 0.22f), p.surface, h * 0.06f,
               p.hairline);
    draw::rect(dl, ImVec2(min.x + w * 0.76f, min.y + h * 0.1f), ImVec2(min.x + w * 0.92f, min.y + h * 0.22f), p.accent, h * 0.06f);
    // Le titre et le sous-titre.
    draw::rect(dl, ImVec2(min.x + w * 0.08f, min.y + h * 0.32f), ImVec2(min.x + w * 0.42f, min.y + h * 0.39f), p.ink, h * 0.035f);
    draw::rect(dl, ImVec2(min.x + w * 0.08f, min.y + h * 0.44f), ImVec2(min.x + w * 0.3f, min.y + h * 0.48f), p.muted, h * 0.02f);
    // Une carte, et deux jaquettes aux couleurs du thème.
    draw::rect(dl, ImVec2(min.x + w * 0.08f, min.y + h * 0.56f), ImVec2(min.x + w * 0.52f, min.y + h * 0.9f), p.surface, h * 0.06f);
    draw::rect(dl, ImVec2(min.x + w * 0.13f, min.y + h * 0.63f), ImVec2(min.x + w * 0.4f, min.y + h * 0.68f), p.body, h * 0.025f);
    draw::rect(dl, ImVec2(min.x + w * 0.13f, min.y + h * 0.74f), ImVec2(min.x + w * 0.3f, min.y + h * 0.82f), p.ink, h * 0.04f);
    const ImU32 c1 = t.covers.empty() ? theme::rgb(0x4C3FB8) : t.covers[0];
    const ImU32 c2 = t.covers.size() > 1 ? t.covers[1] : theme::rgb(0x0F766E);
    draw::rect(dl, ImVec2(min.x + w * 0.58f, min.y + h * 0.56f), ImVec2(min.x + w * 0.73f, min.y + h * 0.9f), c1, h * 0.04f);
    draw::rect(dl, ImVec2(min.x + w * 0.77f, min.y + h * 0.56f), ImVec2(min.x + w * 0.92f, min.y + h * 0.9f), c2, h * 0.04f);
}

// Un bouton-pastille qui montre une couleur ; un clic ouvre le sélecteur
// (roue des teintes, code #RRGGBB, couleurs proposées). value : "#RRGGBB",
// vide = fallback (la couleur du thème). Renvoie true quand la couleur change.
bool colorButton(const char* id, std::string& value, ImU32 fallback, bool allowDefault)
{
    ImU32 color = fallback;
    theme::parseColor(value, color);
    ImGui::PushID(id);
    const ImVec2 size = px(150.0f, 38.0f);
    const bool clicked = ImGui::InvisibleButton("##swatch", size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw::rect(dl, p, ImVec2(p.x + size.x, p.y + size.y), hovered ? theme::SurfaceAlt : theme::Surface, size.y / 2,
               theme::Hairline);
    dl->AddCircleFilled(ImVec2(p.x + px(19.0f), p.y + size.y / 2), px(11.0f), color, 24);
    dl->AddCircle(ImVec2(p.x + px(19.0f), p.y + size.y / 2), px(11.0f), theme::withAlpha(theme::Ink, 40), 24, px(1.0f));
    const std::string label = value.empty() && allowDefault ? std::string("Du thème") : theme::formatColor(color);
    draw::textEllipsis(dl, ImVec2(p.x + px(38.0f), p.y + px(10.0f)), Weight::Bold, 13.0f, theme::Ink, label,
                       size.x - px(38.0f + 32.0f));
    draw::icon(dl, draw::Icon::ChevronDown, ImVec2(p.x + size.x - px(28.0f), p.y + px(11.0f)), px(16.0f), theme::Muted);
    if (clicked)
        ImGui::OpenPopup("##picker");

    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(p.x + size.x - px(280.0f), p.y + size.y + px(8.0f)), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(10.0f, 8.0f));
    if (ImGui::BeginPopup("##picker")) {
        // Le sélecteur d'ImGui : un carré (saturation, clarté), une barre de
        // teintes et le code hexadécimal.
        float rgbValue[3] = {static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                             static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                             static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f};
        ImGui::PushFont(theme::font(Weight::SemiBold), px(13.0f));
        ImGui::SetNextItemWidth(px(248.0f));
        if (ImGui::ColorPicker3("##wheel", rgbValue,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                    ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHex |
                                    ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoOptions)) {
            value = theme::formatColor(ImGui::ColorConvertFloat4ToU32(ImVec4(rgbValue[0], rgbValue[1], rgbValue[2], 1.0f)));
            changed = true;
        }
        ImGui::PopFont();
        // Les couleurs proposées, en petites pastilles.
        space(4.0f);
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        for (int i = 0; i < static_cast<int>(std::size(kSwatches)); ++i) {
            if (i % 6 != 0)
                ImGui::SameLine(0.0f, px(8.0f));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##preset", px(34.0f, 34.0f))) {
                value = theme::formatColor(theme::rgb(kSwatches[i]));
                changed = true;
            }
            const ImVec2 c((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
                           (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
            pdl->AddCircleFilled(c, px(15.0f), theme::rgb(kSwatches[i]), 24);
            pdl->AddCircle(c, px(15.0f), theme::withAlpha(theme::Ink, ImGui::IsItemHovered() ? 120 : 35), 24, px(1.5f));
            ImGui::PopID();
        }
        if (allowDefault) {
            space(2.0f);
            if (button("Couleur du thème##default", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Refresh, .height = 34.0f,
                                                      .fontSize = 13.0f, .disabled = value.empty()})) {
                value.clear();
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return changed;
}

} // namespace

void SettingsScreen::drawAppearance(UiContext& ctx, float width, bool& changed)
{
    Settings& s = ctx.settings;
    // Pendant qu'on fait glisser une couleur, elle s'applique tout de suite
    // (sans écrire settings.ini à chaque image) ; on enregistre en lâchant.
    bool live = false;
    auto colorChanged = [&] {
        live = true;
        m_colorPending = true;
    };

    // --- Les thèmes, en vignettes.
    overline("THÈME");
    space(2.0f);
    {
        const std::vector<theme::ThemeInfo>& list = theme::themes();
        const float gap = px(14.0f);
        const int columns = std::max(2, static_cast<int>((width + gap) / (px(150.0f) + gap)));
        const float cardW = (width - gap * (columns - 1)) / columns;
        const float previewH = cardW * 0.62f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < list.size(); ++i) {
            const theme::ThemeInfo& t = list[i];
            if (i % columns != 0)
                ImGui::SameLine(0.0f, gap);
            ImGui::PushID(t.id.c_str());
            const bool clicked = ImGui::InvisibleButton("##theme", ImVec2(cardW, previewH + px(34.0f)));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 m = ImGui::GetItemRectMin();
            const bool selected = s.theme == t.id;
            const ImVec2 pmax(m.x + cardW, m.y + previewH);
            themePreview(dl, t, m, pmax, px(14.0f));
            if (selected)
                dl->AddRect(ImVec2(m.x - px(3.0f), m.y - px(3.0f)), ImVec2(pmax.x + px(3.0f), pmax.y + px(3.0f)), theme::Accent,
                            px(16.0f), 0, px(2.5f));
            else
                dl->AddRect(m, pmax, hovered ? theme::Dashed : theme::Hairline, px(14.0f), 0, px(1.0f));
            draw::textEllipsis(dl, ImVec2(m.x + px(2.0f), pmax.y + px(9.0f)), Weight::Bold, 14.0f,
                               selected ? theme::Ink : theme::Body, t.name, cardW - px(26.0f));
            if (selected)
                draw::icon(dl, draw::Icon::Check, ImVec2(m.x + cardW - px(20.0f), pmax.y + px(9.0f)), px(18.0f), theme::Accent);
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked && !selected) {
                // Un nouveau thème repart de ses propres couleurs (on garde
                // seulement un dégradé ou une image choisis).
                s.theme = t.id;
                s.accentColor.clear();
                if (s.background == 1)
                    s.background = 0;
                changed = true;
            }
        }
        space(10.0f);
    }

    // --- Couleurs et fond.
    overline("COULEURS ET FOND");
    const theme::ThemeInfo& base = theme::findTheme(s.theme);
    settingRow("Couleur d'accent", "Boutons, liens, sélection", width, px(150.0f), [&] {
        if (colorButton("##accent", s.accentColor, base.palette.accent, true))
            colorChanged();
        return false;
    });
    static const char* const backgrounds[] = {"Du thème", "Couleur", "Dégradé", "Image"};
    changed |= settingRow("Fond de l'interface", "Derrière les pages : uni, dégradé de deux couleurs ou image", width,
                          segmentedWidth({"Du thème", "Couleur", "Dégradé", "Image"}),
                          [&] { return segmented("##background", backgrounds, &s.background); });
    if (s.background == 1) {
        settingRow("Couleur de fond", "Les cartes et les textes s'adaptent pour rester lisibles", width, px(150.0f), [&] {
            if (colorButton("##ground", s.groundColor, base.palette.ground, true))
                colorChanged();
            return false;
        });
    } else if (s.background == 2) {
        settingRow("Couleurs du dégradé", "De la première à la seconde, dans la direction choisie", width,
                   px(150.0f + 10.0f + 150.0f), [&] {
                       if (colorButton("##from", s.gradientFrom, theme::rgb(0x5865F2), false))
                           colorChanged();
                       ImGui::SameLine(0.0f, px(10.0f));
                       if (colorButton("##to", s.gradientTo, theme::rgb(0xEB459E), false))
                           colorChanged();
                       return false;
                   });
        // Les dégradés tout prêts : des pastilles rondes.
        settingRow("Dégradés proposés", "Un clic pour l'essayer", width,
                   px(44.0f) * static_cast<float>(std::size(kGradients)) - px(8.0f), [&] {
                       ImDrawList* dl = ImGui::GetWindowDrawList();
                       for (int i = 0; i < static_cast<int>(std::size(kGradients)); ++i) {
                           if (i > 0)
                               ImGui::SameLine(0.0f, px(8.0f));
                           ImGui::PushID(i);
                           const bool pick = ImGui::InvisibleButton("##grad", px(36.0f, 36.0f));
                           const bool over = ImGui::IsItemHovered();
                           ImGui::PopID();
                           const ImVec2 m = ImGui::GetItemRectMin();
                           const ImVec2 mx = ImGui::GetItemRectMax();
                           theme::gradientRect(dl, m, mx, theme::rgb(kGradients[i].from), theme::rgb(kGradients[i].to), 45, px(18.0f));
                           const bool current = s.gradientFrom == theme::formatColor(theme::rgb(kGradients[i].from)) &&
                                                s.gradientTo == theme::formatColor(theme::rgb(kGradients[i].to));
                           if (current || over)
                               dl->AddCircle(ImVec2((m.x + mx.x) / 2, (m.y + mx.y) / 2), px(20.0f),
                                             current ? theme::Accent : theme::Dashed, 32, px(2.0f));
                           if (pick) {
                               s.gradientFrom = theme::formatColor(theme::rgb(kGradients[i].from));
                               s.gradientTo = theme::formatColor(theme::rgb(kGradients[i].to));
                               changed = true;
                           }
                       }
                       return false;
                   });
        if (settingRow("Direction", "0° : de gauche à droite · 90° : de haut en bas", width, px(260.0f),
                       [&] { return slider("##angle", &s.gradientAngle, 0, 359, 200.0f, "°"); }))
            colorChanged();
    } else if (s.background == 3) {
        const std::string current = s.backgroundImage.empty() ? std::string("Aucune image choisie") : s.backgroundImage;
        settingRow("Image de fond", current.c_str(), width, px(190.0f), [&] {
            if (button("Choisir une image…", {.style = s.backgroundImage.empty() ? ButtonStyle::Primary : ButtonStyle::Outline,
                                              .icon = draw::Icon::Image, .height = 38.0f, .width = 190.0f, .fontSize = 13.0f}))
                ctx.openDialog(DialogPurpose::ChooseBackground);
            return false;
        });
        if (settingRow("Voile sur l'image", "Plus fort : l'image s'efface derrière le texte", width, px(260.0f),
                       [&] { return slider("##dim", &s.backgroundDim, 0, 90, 200.0f, " %"); }))
            colorChanged();
    }
    space(10.0f);

    // --- Police.
    overline("POLICE");
    {
        const std::string fontName = s.uiFont.empty()
                                         ? std::string("Plus Jakarta Sans (police d'origine)")
                                         : toUtf8(fromUtf8(s.uiFont).stem());
        settingRow("Police de l'interface", fontName.c_str(), width, px(130.0f + 10.0f + 170.0f), [&] {
            if (button("Importer…##font", {.style = ButtonStyle::Outline, .icon = draw::Icon::Upload, .height = 38.0f,
                                           .width = 130.0f, .fontSize = 13.0f,
                                           .tooltip = "Un fichier .ttf ou .otf, utilisé pour toute l'interface"}))
                ctx.openDialog(DialogPurpose::ChooseFont);
            ImGui::SameLine(0.0f, px(10.0f));
            if (button("Police d'origine", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Refresh, .height = 38.0f,
                                            .width = 170.0f, .fontSize = 13.0f, .disabled = s.uiFont.empty()})) {
                s.uiFont.clear();
                changed = true;
            }
            return false;
        });
    }
    space(10.0f);

    // --- Sons de l'interface.
    overline("SONS DE L'INTERFACE");
    changed |= settingRow("Sons de l'interface", "Clics, survols, validation, retour et notifications (jamais pendant un jeu)",
                          width, px(46.0f), [&] { return toggle("##uiSounds", &s.uiSounds); });
    if (s.uiSounds) {
        changed |= settingRow("Volume des sons", "Indépendant du volume des jeux", width, px(260.0f),
                              [&] { return slider("##uiVolume", &s.uiSoundVolume, 0, 100, 200.0f, " %"); });
        static const char* const packs[] = {"Du thème", "Doux", "8 bits", "Arcade"};
        if (settingRow("Style des sons", "Calculés par l'émulateur : aucun fichier audio", width,
                       segmentedWidth({"Du thème", "Doux", "8 bits", "Arcade"}),
                       [&] { return segmented("##uiPack", packs, &s.uiSoundPack); })) {
            changed = true;
            const int pack = s.uiSoundPack > 0 ? s.uiSoundPack - 1 : base.soundPack;
            sounds::preview(static_cast<sounds::Pack>(pack));
        }
    }
    space(10.0f);

    // --- Bibliothèque.
    overline("BIBLIOTHÈQUE");
    {
        std::vector<const char*> views;
        for (int i = 0; i < kLibraryViewCount; ++i)
            views.push_back(libraryViewName(i));
        changed |= settingRow("Vue de la bibliothèque", "Aussi dans le menu « Vue » de la bibliothèque", width, px(220.0f),
                              [&] { return combo("##libraryView", views, &s.libraryView, 220.0f); });
    }
    space(14.0f);
    if (button("Réinitialiser l'apparence", {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 40.0f})) {
        const Settings defaults;
        s.theme = defaults.theme;
        s.accentColor.clear();
        s.background = 0;
        s.groundColor.clear();
        s.gradientFrom = defaults.gradientFrom;
        s.gradientTo = defaults.gradientTo;
        s.gradientAngle = defaults.gradientAngle;
        s.backgroundDim = defaults.backgroundDim;
        s.uiFont.clear();
        s.uiSounds = defaults.uiSounds;
        s.uiSoundVolume = defaults.uiSoundVolume;
        s.uiSoundPack = 0;
        changed = true;
    }
    space(6.0f);
    textWrapped("D'autres thèmes peuvent être ajoutés sans rien installer : un fichier texte .ini par thème dans le "
                "dossier themes (couleurs, fond, police, sons), lu au démarrage.",
                13.0f, Weight::Medium, theme::Muted, width);

    // Couleur en cours de réglage : appliquée tout de suite, enregistrée en lâchant la souris.
    if (live)
        theme::applySettings(s);
    if (m_colorPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_colorPending = false;
        changed = true;
    }
}

void SettingsScreen::drawCoreOptions(UiContext& ctx, float width)
{
    namespace fs = std::filesystem;
    // Les cores installés (les DLL du dossier cores/), par ordre alphabétique.
    std::vector<std::string> cores;
    std::error_code ec;
    for (auto it = fs::directory_iterator(ctx.dir("cores"), ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".dll")
            cores.push_back(toUtf8(it->path().filename()));
    std::sort(cores.begin(), cores.end());
    if (cores.empty()) {
        text("Aucun core installé : placez des fichiers .dll dans le dossier cores.", 14.0f, Weight::Medium, theme::Muted);
        return;
    }
    m_coreChoice = std::clamp(m_coreChoice, 0, static_cast<int>(cores.size()) - 1);

    // Choix du core.
    std::vector<const char*> names;
    for (const std::string& c : cores)
        names.push_back(c.c_str());
    combo("##core", names, &m_coreChoice, 320.0f);
    const std::string& core = cores[m_coreChoice];
    const std::string optionsFile = "core-options.txt";

    // Charger un core prend du temps (MAME : plus d'une seconde) : on
    // affiche d'abord "Lecture…", et on le charge à l'image suivante.
    if (core != m_inspectedCore) {
        if (m_inspectWait == 0)
            m_inspectWait = 2;
        if (--m_inspectWait > 0) {
            space(8.0f);
            text("Lecture des options de " + core + "…", 14.0f, Weight::SemiBold, theme::Muted);
            return;
        }
        m_coreOptions = ctx.inspectCore(ctx.dir("cores") / fromUtf8(core));
        m_optionValues = readCoreOptions(ctx.dataDir / optionsFile);
        m_inspectedCore = core;
    }
    space(4.0f);
    if (m_coreOptions.empty()) {
        textWrapped("Ce core n'annonce ses options qu'une fois un jeu lancé (c'est le cas de FBNeo). Lancez un jeu une "
                    "fois avec lui : ses options apparaîtront ici.",
                    14.0f, Weight::Medium, theme::Muted, width);
        return;
    }

    // Combien sont changées ? Et un bouton pour tout remettre.
    int changed = 0;
    for (const CoreOption& o : m_coreOptions) {
        const auto it = m_optionValues.find(o.key);
        changed += it != m_optionValues.end() && it->second != o.defaultValue;
    }
    bool save = false;
    text(std::to_string(m_coreOptions.size()) + " options · " +
             (changed ? std::to_string(changed) + (changed > 1 ? " modifiées" : " modifiée") : std::string("toutes par défaut")),
         13.0f, Weight::SemiBold, theme::Muted);
    if (changed) {
        sameLine(12.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8.0f));
        if (button("Tout remettre par défaut", {.style = ButtonStyle::Ghost, .icon = draw::Icon::Refresh, .height = 30.0f,
                                                .fontSize = 13.0f})) {
            for (const CoreOption& o : m_coreOptions)
                m_optionValues.erase(o.key);
            save = true;
        }
    }
    space(4.0f);

    // Une ligne par option, avec la liste de ses valeurs.
    const float controlW = 280.0f;
    for (const CoreOption& o : m_coreOptions) {
        if (o.values.empty())
            continue;
        const auto it = m_optionValues.find(o.key);
        const std::string& current = it != m_optionValues.end() ? it->second : o.defaultValue;
        int index = 0;
        std::vector<std::string> labels;
        for (size_t i = 0; i < o.values.size(); ++i) {
            const auto& [value, label] = o.values[i];
            // Les valeurs les plus courantes, en français.
            labels.push_back(label == "enabled" || label == "true"    ? "Activé"
                             : label == "disabled" || label == "false" ? "Désactivé"
                                                                        : label);
            if (value == o.defaultValue)
                labels.back() += " (défaut)";
            if (value == current)
                index = static_cast<int>(i);
        }
        std::vector<const char*> items;
        for (const std::string& l : labels)
            items.push_back(l.c_str());
        const bool modified = current != o.defaultValue;
        const std::string label = modified ? "● " + o.label : o.label;
        // Explication : la première ligne seulement (le reste en infobulle).
        std::string hint = o.info.substr(0, o.info.find('\n'));
        if (hint.empty())
            hint = o.key;
        ImGui::PushID(o.key.c_str());
        settingRow(label.c_str(), hint.c_str(), width, px(controlW), [&] {
            if (!combo("##value", items, &index, controlW))
                return false;
            const std::string& value = o.values[static_cast<size_t>(index)].first;
            if (value == o.defaultValue)
                m_optionValues.erase(o.key); // le fichier ne garde que ce qui diffère
            else
                m_optionValues[o.key] = value;
            save = true;
            return true;
        });
        if (!o.info.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && o.info.size() > hint.size())
            ImGui::SetTooltip("%s", o.info.c_str());
        ImGui::PopID();
    }
    if (save)
        writeCoreOptions(ctx.dataDir / optionsFile, m_optionValues);
    space(6.0f);
    textWrapped("En ligne, tous les joueurs doivent avoir les mêmes options : sinon le salon refuse de lancer la partie.",
                13.0f, Weight::Medium, theme::Muted, width);
}

// --- Mises à jour de Nexo Retro --------------------------------------------------

namespace {

// Les notes de version sont écrites en Markdown sur GitHub ("## Nouveautés",
// "- une ligne"). Sans moteur Markdown, on retire juste les signes les plus
// visibles pour qu'elles restent agréables à lire. Les notes très longues
// sont coupées : la page de téléchargement a le reste.
std::string readableNotes(const std::string& markdown)
{
    std::istringstream lines(markdown);
    std::string line, out;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && line[start] == '#')
            ++start;
        if (start > 0) {
            while (start < line.size() && line[start] == ' ')
                ++start;
            line = line.substr(start);
        } else if (line.starts_with("- ") || line.starts_with("* ")) {
            line = "• " + line.substr(2);
        }
        // "**gras**" : on garde le mot, sans les étoiles.
        for (size_t at; (at = line.find("**")) != std::string::npos;)
            line.erase(at, 2);
        out += line + '\n';
    }
    while (!out.empty() && out.back() == '\n')
        out.pop_back();
    if (out.size() > 1500) {
        size_t cut = out.rfind('\n', 1500);
        out = out.substr(0, cut == std::string::npos ? 1500 : cut) + "\n…";
    }
    return out;
}

} // namespace

void SettingsScreen::drawUpdates(UiContext& ctx, float width)
{
    if (!ctx.appUpdater)
        return;
    using State = AppUpdater::State;
    AppUpdater& updater = *ctx.appUpdater;
    Settings& s = ctx.settings;
    const State state = updater.state();
    const bool busy = state == State::Checking || state == State::Downloading;
    const bool installed = updater.installedCopy();
    const std::string& latest = updater.latestVersion();
    const bool ignored = !latest.empty() && latest == s.appIgnoredVersion;
    const bool online = ctx.session.inSalon();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // --- La version de ce programme, et le bouton "Rechercher".
    {
        const std::string version = std::string("Nexo Retro ") + AppUpdater::currentVersion();
        const char* hint = installed ? "Copie installée : les mises à jour s'installent en un clic"
                                     : "Copie portable : les mises à jour se téléchargent depuis la page officielle";
        const char* action = "Rechercher une mise à jour";
        const float valueW = draw::measure(Weight::SemiBold, 13.0f, version).x;
        const float actionW = draw::measure(Weight::Bold, 13.0f, action).x + px(28.0f + 17.0f + 8.0f);
        settingRow("Version actuelle", hint, width, valueW + px(12.0f) + actionW, [&] {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            draw::text(dl, ImVec2(p.x, p.y + px(11.0f)), Weight::SemiBold, 13.0f, theme::Body, version);
            ImGui::SetCursorScreenPos(ImVec2(p.x + valueW + px(12.0f), p.y));
            if (button(action, {.style = ButtonStyle::Outline, .icon = draw::Icon::Refresh, .height = 38.0f,
                                .fontSize = 13.0f, .disabled = busy || state == State::Ready}))
                updater.check(false);
            return false;
        });
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8.0f));

    // --- L'état : vérification, à jour, nouvelle version, téléchargement...
    switch (state) {
    case State::Idle:
        text(s.appLastCheck > 0 ? "Dernière vérification : " + formatAgo(s.appLastCheck)
                                : std::string("Aucune vérification pour l'instant"),
             14.0f, Weight::SemiBold, theme::Muted);
        break;
    case State::Checking:
        pill("Recherche d'une mise à jour…", Tone::Info, 13.0f);
        break;
    case State::UpToDate:
        pill("Nexo Retro est à jour", Tone::Success, 13.0f);
        space(2.0f);
        text("Dernière version publiée : " + latest, 13.0f, Weight::Medium, theme::Muted);
        break;
    case State::Error:
        // Vérification impossible (pas d'Internet, dépôt privé...) : discret.
        // Téléchargement ou empreinte refusés : en rouge.
        pill(updater.error().c_str(), latest.empty() ? Tone::Neutral : Tone::Danger, 13.0f);
        if (latest.empty())
            break;
        space(6.0f);
        [[fallthrough]]; // (on affiche quand même la version trouvée et ses notes)
    case State::Available:
    case State::Downloading:
    case State::Ready: {
        text("Nouvelle version : " + latest, 20.0f, Weight::ExtraBold);
        if (ignored) {
            sameLine(12.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4.0f));
            pill("Version ignorée", Tone::Neutral);
        }
        if (!updater.notes().empty()) {
            space(4.0f);
            overline("NOTES DE VERSION");
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
            textWrapped(readableNotes(updater.notes()), 13.0f, Weight::Medium, theme::Body, width);
        }
        space(8.0f);

        if (state == State::Downloading) {
            // Barre de progression.
            const float p = updater.progress();
            const float barW = std::min(width, px(420.0f));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            draw::rect(dl, at, ImVec2(at.x + barW, at.y + px(10.0f)), theme::Track, px(5.0f));
            if (p > 0.0f)
                draw::rect(dl, at, ImVec2(at.x + barW * std::min(p, 1.0f), at.y + px(10.0f)), theme::Accent, px(5.0f));
            ImGui::Dummy(ImVec2(barW, px(10.0f)));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4.0f));
            text(p >= 0.0f ? "Téléchargement : " + std::to_string(static_cast<int>(p * 100)) + " %"
                           : std::string("Téléchargement…"),
                 13.0f, Weight::SemiBold, theme::Muted);
            break;
        }
        if (state == State::Ready) {
            pill("Prête à installer", Tone::Success, 13.0f);
            space(2.0f);
            textWrapped(s.appAutoInstall ? "Elle s'installera quand vous quitterez Nexo Retro, ou tout de suite avec le bouton."
                                         : "Nexo Retro va se fermer, s'installer puis se relancer tout seul. Vos jeux et "
                                           "sauvegardes ne bougent pas.",
                        13.0f, Weight::Medium, theme::Muted, width);
            space(4.0f);
        }
        // Les boutons.
        if (installed) {
            if (button("Installer maintenant", {.style = ButtonStyle::Accent, .icon = draw::Icon::Upload, .height = 42.0f,
                                                .fontSize = 14.0f, .disabled = online}))
                ctx.installUpdate();
            sameLine();
        }
        if (button("Ouvrir la page de téléchargement",
                   {.style = installed ? ButtonStyle::Outline : ButtonStyle::Accent, .height = 42.0f, .fontSize = 14.0f,
                    .disabled = updater.pageUrl().empty()}))
            SDL_OpenURL(updater.pageUrl().c_str());
        sameLine();
        if (!ignored) {
            if (button("Ignorer cette version", {.style = ButtonStyle::Ghost, .height = 42.0f, .fontSize = 14.0f})) {
                s.appIgnoredVersion = latest;
                ctx.settingsChanged();
            }
        } else if (button("Ne plus ignorer", {.style = ButtonStyle::Ghost, .height = 42.0f, .fontSize = 14.0f})) {
            s.appIgnoredVersion.clear();
            ctx.settingsChanged();
        }
        if (installed && online) {
            space(2.0f);
            text("Indisponible pendant une partie en ligne", 13.0f, Weight::SemiBold, theme::Warning);
        } else if (!installed) {
            space(2.0f);
            textWrapped("Copie portable : téléchargez la nouvelle version et remplacez les fichiers du programme. "
                        "Vos données (jeux, sauvegardes, réglages) restent dans leur dossier.",
                        13.0f, Weight::Medium, theme::Muted, width);
        }
        break;
    }
    }
    space(12.0f);

    // --- Les réglages.
    bool changed = false;
    changed |= settingRow("Rechercher automatiquement les mises à jour", "Au démarrage, au plus une fois par jour", width,
                          px(46.0f), [&] { return toggle("##appAutoCheck", &s.appAutoCheck); });
    changed |= settingRow("Installer automatiquement les mises à jour",
                          installed ? "Téléchargées en arrière-plan, installées quand vous quittez Nexo Retro"
                                    : "Seulement pour la copie installée (pas la version portable)",
                          width, px(46.0f), [&] { return toggle("##appAutoInstall", &s.appAutoInstall, !installed); });
    if (changed)
        ctx.settingsChanged();
    space(6.0f);
    textWrapped("Les mises à jour viennent des versions officielles de Nexo Retro publiées sur GitHub. Chaque fichier "
                "téléchargé est vérifié (empreinte SHA-256) avant d'être installé.",
                13.0f, Weight::Medium, theme::Muted, width);
}

void SettingsScreen::draw(UiContext& ctx)
{
    Settings& s = ctx.settings;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float navW = px(260.0f);
    const std::vector<std::string> pads = ctx.input.gamepadNames();
    if (ctx.nav.settingsTab >= 0) {
        m_tab = ctx.nav.settingsTab;
        ctx.nav.settingsTab = -1;
    }
    if (ctx.nav.settingsDevice >= -1) {
        m_device = ctx.nav.settingsDevice;
        ctx.nav.settingsDevice = -2;
    }
    // Changer d'onglet annule une capture de touche en cours.
    const int previousTab = m_tab;

    // --- Onglets.
    ImGui::BeginGroup();
    pageTitle("Paramètres", "");
    space(4.0f);
    // Chaque onglet a un numéro fixe (utilisé par --settings-tab et par les
    // autres écrans), mais ils s'affichent dans l'ordre de cette liste :
    // « Apparence » (n° 10, ajouté après les autres) vient après « Affichage ».
    struct Tab {
        const char* label;
        int id;
    };
    static const Tab tabs[] = {{"Affichage", 0},           {"Apparence", 10},           {"Son", 1},
                               {"Manettes", 2},            {"Sauvegardes", 3},          {"Dossiers", 4},
                               {"Jeu en ligne", 5},        {"Jaquettes et infos", 6},   {"Options des cores", 7},
                               {"Émulateurs externes", 8}, {"Émulateurs (cores)", 9}, {"Mises à jour", 11}};

    // Les cores à installer l'un après l'autre (page "Émulateurs") : le
    // suivant part dès que le précédent est fini.
    if (!m_coreQueue.empty() && !ctx.installer.busy()) {
        ctx.installer.startCore(m_coreQueue.front(), ctx.library.coresDir());
        m_coreQueue.pop_front();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(std::size(tabs)); ++i) {
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(navW, px(46.0f))))
            m_tab = tabs[i].id;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin();
        const bool selected = m_tab == tabs[i].id;
        if (selected || hovered)
            draw::rect(dl, min, ImVec2(min.x + navW, min.y + px(46.0f)), selected ? theme::Surface : theme::withAlpha(theme::Surface, 130),
                       px(14.0f));
        draw::text(dl, ImVec2(min.x + px(16.0f), min.y + px(13.0f)), Weight::Bold, 15.0f,
                   selected ? theme::Ink : theme::Muted, tabs[i].label);
        if (tabs[i].id == 2 && !pads.empty()) {
            const std::string hint = std::to_string(pads.size()) + (pads.size() > 1 ? " branchées" : " branchée");
            const float hw = draw::measure(Weight::SemiBold, 12.0f, hint).x;
            draw::text(dl, ImVec2(min.x + navW - px(16.0f) - hw, min.y + px(16.0f)), Weight::SemiBold, 12.0f, theme::Muted, hint);
        }
        if (i + 1 < static_cast<int>(std::size(tabs)))
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    }
    ImGui::EndGroup();
    if (m_tab != previousTab) {
        ctx.input.cancelCapture();
        ctx.voice.setTesting(false); // (le test du micro s'arrête en quittant "Jeu en ligne")
        m_waitingVoiceKey = false;
    }

    // --- Panneau.
    ImGui::SameLine(0.0f, px(32.0f));
    beginCard("##panel", ImVec2(avail.x - navW - px(32.0f), avail.y), 30.0f, theme::Surface, 24.0f, true);
    // Tests : EMU_TEST_SCROLL=N fait défiler le panneau de N pixels (pour
    // voir le bas d'un long onglet sur une capture).
    if (const char* scroll = SDL_getenv("EMU_TEST_SCROLL"))
        ImGui::SetScrollY(px(static_cast<float>(SDL_atoi(scroll))));
    const float w = ImGui::GetContentRegionAvail().x;
    bool changed = false;
    auto header = [&](const char* title, const char* intro) {
        text(title, 24.0f, Weight::ExtraBold);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        text(intro, 14.0f, Weight::Medium, theme::Muted);
        space(10.0f);
    };
    auto valueRow = [&](const char* label, const char* hint, const std::string& value, const char* action,
                        const std::filesystem::path& folder) {
        const float valueW = draw::measure(Weight::SemiBold, 13.0f, value).x;
        const float actionW = action ? draw::measure(Weight::Bold, 13.0f, action).x + px(28.0f) : 0.0f;
        settingRow(label, hint, w, valueW + (action ? px(12.0f) + actionW : 0.0f), [&] {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            draw::text(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + px(11.0f)), Weight::SemiBold, 13.0f, theme::Body, value);
            if (action) {
                ImGui::SetCursorScreenPos(ImVec2(p.x + valueW + px(12.0f), p.y));
                if (button(action, {.style = ButtonStyle::Outline, .height = 38.0f, .fontSize = 13.0f}))
                    openFolder(folder);
            }
            return false;
        });
    };

    switch (m_tab) {
    case 0: {
        header("Affichage", "Comment l'image du jeu s'affiche sur votre écran.");
        {
            // La langue de l'interface : le français (celle du code) + un
            // fichier par langue dans assets/lang/.
            const std::vector<lang::Language> languages =
                lang::available(fromUtf8(SDL_GetBasePath()) / "assets" / "lang");
            std::vector<const char*> names;
            int current = 0;
            for (size_t i = 0; i < languages.size(); ++i) {
                names.push_back(languages[i].name.c_str());
                if (languages[i].code == s.language)
                    current = static_cast<int>(i);
            }
            changed |= settingRow("Langue", "La langue de l'interface", w, px(220.0f), [&] {
                if (!combo("##language", names, &current, 220.0f))
                    return false;
                s.language = languages[static_cast<size_t>(current)].code;
                return true;
            });
        }
        changed |= settingRow("Plein écran au lancement", "Sinon le jeu s'ouvre dans la fenêtre (F11 pour basculer)", w,
                              px(46.0f), [&] { return toggle("##fs", &s.fullscreenOnLaunch); });
        changed |= settingRow("Démarrer en Big Picture", "Plein écran, grandes jaquettes, tout à la manette (Start pour l'ouvrir)",
                              w, px(46.0f), [&] { return toggle("##bigpicture", &s.bigPictureOnStart); });
        static const char* const scaling[] = {"Pixels nets", "Lissé"};
        changed |= settingRow("Agrandissement", "Des pixels nets ou une image adoucie", w,
                              segmentedWidth({"Pixels nets", "Lissé"}), [&] { return segmented("##scale", scaling, &s.scaling); });
        static const char* const aspects[] = {"Original", "4:3", "16:9"};
        changed |= settingRow("Format d'image", "Les proportions de l'image", w, segmentedWidth({"Original", "4:3", "16:9"}),
                              [&] { return segmented("##aspect", aspects, &s.aspect); });
        changed |= settingRow("Afficher les images par seconde", "Dans un coin de l'écran pendant le jeu", w, px(46.0f),
                              [&] { return toggle("##fps", &s.showFps); });
        changed |= settingRow("Rendu Vulkan", "Pour les cores 3D qui savent faire les deux (PSX HW, PPSSPP, Dolphin...)", w,
                              px(46.0f), [&] { return toggle("##vulkan", &s.preferVulkan); });
        break;
    }
    case 1: {
        header("Son", "Le son des jeux.");
        valueRow("Sortie audio", "Le périphérique utilisé", "Par défaut du système", nullptr, {});
        changed |= settingRow("Volume", "Aussi réglable en jeu, dans le menu (Échap)", w, px(260.0f),
                              [&] { return slider("##volume", &s.volume, 0, 100, 200.0f, " %"); });
        static const char* const latency[] = {"Basse", "Normale", "Sûre"};
        changed |= settingRow("Latence", "Plus basse = plus réactif, mais risque de grésillements", w,
                              segmentedWidth({"Basse", "Normale", "Sûre"}), [&] { return segmented("##lat", latency, &s.audioLatency); });
        break;
    }
    case 2: {
        header("Manettes", "Toutes les manettes sont acceptées. Elles deviennent joueur 1, 2, 3... dans l'ordre de branchement.");
        {
            // Le clavier : avec la 1re manette (jouer seul), ou un joueur à part
            // (jouer à deux sur un même PC avec une seule manette).
            static const char* const keyboard[] = {"Avec la manette 1", "Joueur à part"};
            int mode = s.keyboardSeparate ? 1 : 0;
            if (settingRow("Le clavier", "Joueur à part : clavier = joueur 1, manettes = joueurs 2, 3, 4", w,
                           segmentedWidth({"Avec la manette 1", "Joueur à part"}),
                           [&] { return segmented("##keyboard", keyboard, &mode); })) {
                s.keyboardSeparate = mode == 1;
                changed = true;
            }
            // Wii : le pointeur de la Wiimote (viser l'écran).
            static const char* const pointer[] = {"Stick droit", "Souris"};
            int aim = s.wiiMousePointer ? 1 : 0;
            if (settingRow("Wii : viser avec", "Le pointeur de la Wiimote. Le type de manette se choisit sur la fiche de chaque jeu Wii.",
                           w, segmentedWidth({"Stick droit", "Souris"}), [&] { return segmented("##wiiaim", pointer, &aim); })) {
                s.wiiMousePointer = aim == 1;
                changed = true;
            }
            space(4.0f);
        }
        drawControls(ctx, w);
        space(6.0f);
        static const char* const menu[] = {"Select + Start", "Bouton central"};
        changed |= settingRow("Ouvrir le menu en jeu", "Le raccourci manette pendant une partie (Échap au clavier)", w,
                              segmentedWidth({"Select + Start", "Bouton central"}), [&] { return segmented("##menu", menu, &s.menuButton); });
        {
            // Avance rapide : la vitesse (x2 à x8).
            static const char* const speeds[] = {"×2", "×3", "×4", "×8"};
            static const int values[] = {2, 3, 4, 8};
            int speed = 2;
            for (int i = 0; i < 4; ++i)
                if (values[i] == s.fastForwardSpeed)
                    speed = i;
            if (settingRow("Avance rapide", "Maintenir Tab, ou Select + R à la manette", w, segmentedWidth({"×2", "×3", "×4", "×8"}),
                           [&] { return segmented("##fast", speeds, &speed); })) {
                s.fastForwardSpeed = values[speed];
                changed = true;
            }
        }
        changed |= settingRow("Retour en arrière",
                              "Maintenir Retour arrière, ou Select + L à la manette (pas en ligne ni pour PS2, GameCube, Wii)", w,
                              px(46.0f), [&] { return toggle("##rewind", &s.rewind); });
        break;
    }
    case 3: {
        header("Sauvegardes", "Ne plus jamais perdre une partie.");
        changed |= settingRow("Sauvegarde automatique", "Un état est créé quand vous quittez un jeu, pour le reprendre", w,
                              px(46.0f), [&] { return toggle("##auto", &s.autoSaveState); });
        valueRow("Raccourcis", "Sauvegarde d'état rapide", "F2 sauver · F4 charger", nullptr, {});
        // Vos BIOS dans un .zip (et retour) : pour les garder de côté ou les
        // recopier sur un autre PC. La suite : Shell::onDialogResult.
        settingRow("Mes BIOS", "Une copie de votre dossier bios dans un .zip, à garder pour vous ou à recopier sur un autre PC",
                   w, px(130.0f + 10.0f + 130.0f), [&] {
                       const ImVec2 p = ImGui::GetCursorScreenPos();
                       if (button("Exporter", {.style = ButtonStyle::Outline, .height = 38.0f, .width = 130.0f, .fontSize = 13.0f}))
                           ctx.openDialog(DialogPurpose::ExportBios);
                       ImGui::SetCursorScreenPos(ImVec2(p.x + px(140.0f), p.y));
                       if (button("Importer", {.style = ButtonStyle::Outline, .height = 38.0f, .width = 130.0f, .fontSize = 13.0f}))
                           ctx.openDialog(DialogPurpose::ImportBios);
                       return false;
                   });
        break;
    }
    case 4: {
        header("Dossiers", "Où l'émulateur range ses fichiers.");
        valueRow("Jeux", "Un sous-dossier par console", toUtf8(ctx.dir("roms")), "Ouvrir", ctx.dir("roms"));
        valueRow("BIOS", "Fichiers système des consoles", toUtf8(ctx.dir("bios")), "Ouvrir", ctx.dir("bios"));
        valueRow("Sauvegardes", "Parties, états et captures", toUtf8(ctx.dir("saves")), "Ouvrir", ctx.dir("saves"));
        valueRow("Émulateurs", "Les cores libretro (.dll)", toUtf8(ctx.dir("cores")), "Ouvrir", ctx.dir("cores"));
        if (const size_t hidden = ctx.userData.hiddenCount(); hidden > 0) {
            const std::string count = std::to_string(hidden) + (hidden > 1 ? " jeux" : " jeu");
            settingRow("Jeux retirés", "Retirés de la bibliothèque, mais toujours sur le disque", w,
                       draw::measure(Weight::SemiBold, 13.0f, count).x + px(12.0f + 130.0f), [&] {
                           const ImVec2 p = ImGui::GetCursorScreenPos();
                           draw::text(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + px(11.0f)), Weight::SemiBold, 13.0f,
                                      theme::Body, count);
                           ImGui::SetCursorScreenPos(
                               ImVec2(p.x + draw::measure(Weight::SemiBold, 13.0f, count).x + px(12.0f), p.y));
                           if (button("Réafficher", {.style = ButtonStyle::Outline, .height = 38.0f, .width = 130.0f,
                                                     .fontSize = 13.0f})) {
                               ctx.userData.unhideAll();
                               ctx.userData.save();
                               ctx.rescan();
                               ctx.notify("Jeux réaffichés");
                           }
                           return false;
                       });
        }
        break;
    }
    case 5: {
        header("Jeu en ligne", "Les parties avec vos amis.");
        if (m_nickname[0] == '\0')
            std::snprintf(m_nickname, sizeof(m_nickname), "%s", s.nickname.c_str());
        changed |= settingRow("Pseudo", "Visible par vos amis", w, px(240.0f), [&] {
            if (textField("##nick", m_nickname, sizeof(m_nickname), "Votre pseudo", 240.0f)) {
                s.nickname = m_nickname;
                return true;
            }
            return false;
        });
        changed |= settingRow("Ping et pertes pendant la partie", "En haut à droite de l'écran (aussi dans le menu pause)", w,
                              px(46.0f), [&] { return toggle("##netStats", &s.netStats); });
        drawVoice(ctx, w, changed);
        drawDiscord(ctx, w, changed);
        break;
    }
    case 6: {
        header("Jaquettes et infos", "Jaquettes, genre, année, éditeur : trouvés en ligne, sans compte.");
        changed |= settingRow("Recherche automatique", "Chercher tout seul les jeux ajoutés à la bibliothèque", w, px(46.0f),
                              [&] { return toggle("##autoScrape", &s.autoScrape); });
        // Combien de jeux sont déjà connus ?
        int found = 0, missing = 0;
        for (const Game& g : ctx.library.games()) {
            const scrape::GameInfo* info = ctx.scraper.find(g.path);
            if (!info)
                ++missing;
            else if (info->status == scrape::GameInfo::Status::Found)
                ++found;
        }
        const int total = static_cast<int>(ctx.library.games().size());
        const int pending = ctx.scraper.pending();
        std::string state = std::to_string(found) + " / " + std::to_string(total) + " jeux reconnus";
        if (pending > 0)
            state += " · " + std::to_string(pending) + " en cours";
        settingRow("Bibliothèque", pending > 0 ? ("Recherche : " + ctx.scraper.current()).c_str()
                                               : "Les jeux introuvables gardent leur jaquette générée",
                   w, draw::measure(Weight::SemiBold, 13.0f, state).x + px(12.0f) + px(190.0f), [&] {
                       const ImVec2 p = ImGui::GetCursorScreenPos();
                       draw::text(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + px(11.0f)), Weight::SemiBold, 13.0f,
                                  theme::Body, state);
                       ImGui::SetCursorScreenPos(ImVec2(p.x + draw::measure(Weight::SemiBold, 13.0f, state).x + px(12.0f), p.y));
                       const bool all = missing == 0;
                       if (button(all ? "Tout rechercher à nouveau" : "Rechercher les manquants",
                                  {.style = ButtonStyle::Outline, .height = 38.0f, .width = 190.0f, .fontSize = 13.0f,
                                   .disabled = pending > 0})) {
                           for (const Game& g : ctx.library.games())
                               ctx.scraper.request(g, all);
                       }
                       return false;
                   });
        valueRow("Images", "Jaquettes, captures et bases téléchargées", "Dossier scraper", "Ouvrir", ctx.scraper.dir());
        space(6.0f);
        textWrapped("Sources : libretro-database (informations) et thumbnails.libretro.com (images), des projets libres "
                    "et communautaires. Les jeux à cartouche sont reconnus à coup sûr grâce à l'empreinte de leur "
                    "fichier ; les jeux sur disque, d'après leur nom.",
                    13.0f, Weight::Medium, theme::Muted, w);
        break;
    }
    case 8:
        header("Émulateurs externes", "Les consoles récentes, lancées par leur propre émulateur (à installer à part).");
        drawExternal(ctx, w);
        break;
    case 7:
        header("Options des cores", "Les réglages propres à chaque émulateur. Appliqués au prochain lancement d'un jeu.");
        drawCoreOptions(ctx, w);
        break;
    case 9:
        header("Émulateurs (cores)", "Le programme qui fait tourner chaque console : installé ou mis à jour en un clic.");
        drawCores(ctx, w);
        break;
    case 10:
        header("Apparence", "Thème, couleurs, fond, police et sons de l'interface.");
        drawAppearance(ctx, w, changed);
        break;
    case 11:
        header("Mises à jour", "Nexo Retro se met à jour depuis ses versions officielles.");
        drawUpdates(ctx, w);
        break;
    default:
        break;
    }
    endCard();

    if (changed)
        ctx.settingsChanged();
}

} // namespace ui
