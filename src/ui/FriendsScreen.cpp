// Amis : le salon privé (créer, rejoindre, membres, jeu, discussion).
//
// Pas de serveur : l'émulateur de l'hôte EST le salon. Un code d'invitation
// permet aux amis de le joindre (voir src/net/Session.hpp).

#include "../Utf8.hpp"
#include "../net/LinkPlay.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

using theme::px;
using theme::Weight;
using State = net::Session::State;
using net::GameStatus;

namespace ui {

namespace {

// Le code d'invitation, en gros, avec un bouton "Copier".
void codeBox(const char* id, const std::string& code, float width)
{
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(64.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw::rect(dl, p, ImVec2(p.x + width, p.y + h), theme::AccentSoft, px(16.0f));
    const float copyW = px(110.0f);
    draw::textEllipsis(dl, ImVec2(p.x + px(18.0f), p.y + px(21.0f)), Weight::ExtraBold, 15.0f, theme::AccentInk, code,
                       width - copyW - px(36.0f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - copyW - px(12.0f), p.y + px(12.0f)));
    if (button("Copier", {.style = ButtonStyle::Primary, .icon = draw::Icon::Copy, .height = 40.0f, .width = 110.0f}))
        SDL_SetClipboardText(code.c_str());
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::PopID();
}

// Les lignes techniques ("Adresse publique : ...") en petit et en gris.
void details(const net::Session& session, float width)
{
    for (const std::string& line : session.details())
        textWrapped(line, 12.0f, Weight::Medium, theme::Muted, width);
}

Tone toneOf(GameStatus s)
{
    switch (s) {
    case GameStatus::Ready: return Tone::Success;
    case GameStatus::Missing: return Tone::Danger;
    case GameStatus::Different: return Tone::Warning;
    case GameStatus::Checking: return Tone::Accent;
    case GameStatus::Unknown: return Tone::Neutral;
    }
    return Tone::Neutral;
}

// Une couleur d'avatar par membre, toujours la même pour un numéro donné.
void avatarColors(uint8_t id, ImU32& bg, ImU32& fg)
{
    static const unsigned bgs[] = {0xE8E4FF, 0xDCF5E6, 0xFFE6D5, 0xE0ECFF, 0xFFE4EC, 0xFEF3C7, 0xE0F2F1, 0xEDE9FE};
    static const unsigned fgs[] = {0x3B2DB8, 0x14532D, 0x9A3412, 0x1E3A8A, 0x9D174D, 0x854D0E, 0x115E59, 0x5B21B6};
    bg = theme::rgb(bgs[id % 8]);
    fg = theme::rgb(fgs[id % 8]);
}

Tone toneOf(net::LinkQuality q)
{
    switch (q) {
    case net::LinkQuality::Excellent:
    case net::LinkQuality::Good: return Tone::Success;
    case net::LinkQuality::Fair: return Tone::Warning;
    case net::LinkQuality::Poor: return Tone::Danger;
    case net::LinkQuality::Unknown: break;
    }
    return Tone::Neutral;
}

ImU32 dotColor(net::LinkQuality q)
{
    switch (q) {
    case net::LinkQuality::Excellent:
    case net::LinkQuality::Good: return theme::rgb(0x22C55E);
    case net::LinkQuality::Fair: return theme::rgb(0xF59E0B);
    case net::LinkQuality::Poor: return theme::rgb(0xEF4444);
    case net::LinkQuality::Unknown: break;
    }
    return theme::Track;
}

// "42 ms · 0 % de pertes"
std::string linkText(const net::Member& m)
{
    if (m.pingMs < 0)
        return "Mesure…";
    std::string t = std::to_string(m.pingMs) + " ms";
    if (m.lossPct >= 0)
        t += " · " + std::to_string(m.lossPct) + " % de pertes";
    return t;
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// --- Créer ou rejoindre -------------------------------------------------------------

void FriendsScreen::drawStart(UiContext& ctx, float inner)
{
    net::Session& session = ctx.session;
    textWrapped("Votre émulateur devient le salon : vos amis le rejoignent avec un code, sans compte ni serveur.", 14.0f,
                Weight::Medium, theme::Muted, inner);
    space(6.0f);
    overline("CRÉER UN SALON");
    textField("##salonName", m_salonName, sizeof(m_salonName), ("Salon de " + ctx.settings.nickname).c_str(), 0.0f);
    textField("##hostPassword", m_hostPassword, sizeof(m_hostPassword), "Mot de passe (facultatif)", 0.0f);
    if (button("Créer le salon", {.style = ButtonStyle::Primary, .icon = draw::Icon::Plus, .height = 46.0f, .width = -1.0f})) {
        session.setNickname(ctx.settings.nickname);
        ctx.online.chat.clear();
        session.host(m_salonName, m_hostPassword);
    }
    space(16.0f);
    overline("REJOINDRE UN AMI");
    textField("##code", m_code, sizeof(m_code), "Code du salon (EMU-...)", 0.0f);
    textField("##joinPassword", m_joinPassword, sizeof(m_joinPassword), "Mot de passe (si le salon en a un)", 0.0f);
    if (button("Rejoindre", {.style = ButtonStyle::Outline, .icon = draw::Icon::Users, .height = 46.0f, .width = -1.0f,
                             .disabled = m_code[0] == '\0'})) {
        session.setNickname(ctx.settings.nickname);
        ctx.online.chat.clear();
        session.join(m_code, m_joinPassword);
    }
}

// --- Membres ------------------------------------------------------------------------

void FriendsScreen::drawMembers(UiContext& ctx, float width)
{
    net::Session& session = ctx.session;
    const auto& members = session.members();
    VoiceChat& voice = ctx.voice;
    const bool voiceOn = ctx.settings.voiceMode != 0;
    {
        // Titre de la liste, et à droite : son micro.
        const ImVec2 top = ImGui::GetCursorPos();
        overline(("MEMBRES · " + std::to_string(members.size()) + " / " + std::to_string(net::Session::kMaxMembers)).c_str());
        const ImVec2 after = ImGui::GetCursorPos();
        if (voiceOn) {
            const bool muted = voice.selfMuted();
            const char* label = muted ? "Micro coupé" : "Micro actif";
            const float bw = draw::measure(Weight::Bold, 12.0f, label).x + px(52.0f);
            ImGui::SetCursorPos(ImVec2(top.x + width - bw, top.y - px(6.0f)));
            if (button(label, {.style = muted ? ButtonStyle::Outline : ButtonStyle::Soft,
                               .icon = muted ? draw::Icon::MicOff : draw::Icon::Mic, .height = 30.0f, .fontSize = 12.0f,
                               .tooltip = muted ? "Vos amis ne vous entendent plus" : "Couper mon micro"}))
                voice.setSelfMuted(!muted);
            ImGui::SetCursorPos(after);
        }
    }
    for (const net::Member& m : members) {
        ImGui::PushID(m.id);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = px(58.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw::rect(dl, p, ImVec2(p.x + width, p.y + h), m.self ? theme::SurfaceAlt : theme::Surface, px(14.0f));
        ImU32 bg, fg;
        avatarColors(m.id, bg, fg);
        const std::string initial = m.name.substr(0, 1);
        // Un anneau vert autour de celui qui parle.
        if (voiceOn && voice.talking(m.id))
            dl->AddCircle(ImVec2(p.x + px(28.0f), p.y + h / 2), px(23.0f), theme::rgb(0x22C55E), 32, px(2.5f));
        avatar(dl, ImVec2(p.x + px(28.0f), p.y + h / 2), px(20.0f), initial.c_str(), bg, fg);

        // Nom (+ "vous" / "hôte") et ping.
        std::string name = m.name + (m.self ? " (vous)" : "");
        const ImVec2 status = pillSize(statusLabel(m.status));
        const bool canKick = session.isHost() && !m.self;
        const bool canMute = voiceOn && !m.self;
        const float buttons = (canKick ? px(40.0f) : 0.0f) + (canMute ? px(40.0f) : 0.0f);
        const float rightW = (session.game() ? status.x : 0.0f) + buttons + px(16.0f);
        draw::textEllipsis(dl, ImVec2(p.x + px(58.0f), p.y + px(10.0f)), Weight::Bold, 14.0f, theme::Ink, name,
                           width - px(70.0f) - rightW);
        // Hôte ou membre ; et la qualité de la liaison (un point de couleur,
        // le ping et les pertes). Pour un membre, celle des autres est
        // mesurée par l'hôte.
        const std::string role = m.host ? "Hôte" : "Membre";
        const ImVec2 subPos(p.x + px(58.0f), p.y + px(31.0f));
        draw::text(dl, subPos, Weight::Medium, 12.0f, theme::Muted, role);
        if (!m.self) {
            const float roleW = draw::measure(Weight::Medium, 12.0f, role).x;
            const net::LinkQuality q = net::qualityOf(m.pingMs, m.lossPct);
            dl->AddCircleFilled(ImVec2(subPos.x + roleW + px(10.0f), subPos.y + px(8.0f)), px(3.5f), dotColor(q));
            draw::textEllipsis(dl, ImVec2(subPos.x + roleW + px(18.0f), subPos.y), Weight::Medium, 12.0f, theme::Muted,
                               linkText(m), width - px(76.0f) - roleW - rightW);
        }

        // État du jeu, puis les boutons : ne plus l'entendre, retirer (hôte).
        float x = p.x + width - px(12.0f) - buttons - status.x;
        if (session.game())
            pillAt(dl, ImVec2(x, p.y + (h - status.y) / 2), statusLabel(m.status), toneOf(m.status));
        if (canMute) {
            const bool muted = voice.memberMuted(m.id);
            ImGui::SetCursorScreenPos(ImVec2(p.x + width - px(44.0f) - (canKick ? px(40.0f) : 0.0f), p.y + (h - px(32.0f)) / 2));
            if (iconButton("##mute", muted ? draw::Icon::SpeakerOff : draw::Icon::Speaker, ButtonStyle::Ghost, 32.0f,
                           muted ? "L'entendre à nouveau" : "Ne plus l'entendre"))
                voice.setMemberMuted(m.id, !muted);
        }
        if (canKick) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + width - px(44.0f), p.y + (h - px(32.0f)) / 2));
            if (iconButton("##kick", draw::Icon::Close, ButtonStyle::Ghost, 32.0f, "Retirer du salon"))
                session.kick(m.id);
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(width, h - px(4.0f)));
        ImGui::PopID();
    }
}

// --- Carte de gauche : le salon -------------------------------------------------------

void FriendsScreen::drawSalon(UiContext& ctx, float width, float height)
{
    net::Session& session = ctx.session;
    beginCard("##salon", ImVec2(width, height), 24.0f, theme::Surface, 24.0f, true);
    const float inner = ImGui::GetContentRegionAvail().x;
    const ImVec2 top = ImGui::GetCursorPos();
    const std::string title = session.inSalon() ? session.salonName() : "Salon privé";
    draw::textEllipsis(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), Weight::ExtraBold, 26.0f, theme::Ink, title,
                       inner - px(130.0f));

    const char* stateText = "Hors ligne";
    Tone tone = Tone::Neutral;
    switch (session.state()) {
    case State::Idle: break;
    case State::Preparing: stateText = "Préparation"; tone = Tone::Warning; break;
    case State::Hosting: stateText = "Vous êtes l'hôte"; tone = Tone::Accent; break;
    case State::Joining: stateText = "Connexion…"; tone = Tone::Warning; break;
    case State::InSalon: stateText = "Connecté"; tone = Tone::Success; break;
    }
    const ImVec2 ps = pillSize(stateText, 12.0f);
    ImGui::SetCursorPos(ImVec2(top.x + inner - ps.x, top.y + px(8.0f)));
    pill(stateText, tone, 12.0f);
    ImGui::SetCursorPos(ImVec2(top.x, top.y + px(48.0f)));

    switch (session.state()) {
    case State::Idle:
        drawStart(ctx, inner);
        break;
    case State::Preparing:
        text("Préparation du salon…", 16.0f, Weight::ExtraBold);
        textWrapped("Recherche de votre adresse sur Internet et ouverture d'un port sur votre box (quelques secondes).",
                    14.0f, Weight::Medium, theme::Muted, inner);
        space(6.0f);
        details(session, inner);
        break;
    case State::Joining: {
        const int seconds = static_cast<int>(session.secondsInState());
        text("Connexion au salon… (" + std::to_string(seconds) + " s)", 16.0f, Weight::ExtraBold);
        if (session.answerCode().empty()) {
            textWrapped("On tente de joindre le PC de l'hôte par toutes les adresses connues.", 13.0f, Weight::Medium,
                        theme::Muted, inner);
        } else {
            textWrapped("La box de l'hôte bloque la connexion. Envoyez-lui ce code de réponse : quand il l'aura collé, "
                        "vos deux box s'ouvriront l'une vers l'autre.",
                        13.0f, Weight::Medium, theme::Muted, inner);
            space(4.0f);
            codeBox("answer", session.answerCode(), inner);
        }
        space(6.0f);
        details(session, inner);
        break;
    }
    case State::Hosting:
        overline("CODE DU SALON");
        codeBox("invite", session.inviteCode(), inner);
        if (m_hostPassword[0] != '\0')
            text("Donnez aussi le mot de passe à vos amis.", 12.0f, Weight::SemiBold, theme::Muted);
        // Le code complet (toutes les adresses) : pour Tailscale et les
        // réseaux où le code court ne suffit pas.
        if (link(m_showFullCode ? "Masquer le code complet" : "Le code ne marche pas ? Code complet", 12.0f))
            m_showFullCode = !m_showFullCode;
        if (m_showFullCode) {
            space(2.0f);
            codeBox("full", session.fullInviteCode(), inner);
        }
        space(6.0f);
        drawMembers(ctx, inner);
        space(8.0f);
        overline("UN AMI N'ARRIVE PAS À SE CONNECTER ?");
        textField("##answer", m_answer, sizeof(m_answer), "Collez son code de réponse", (inner - px(110.0f)) / theme::scale());
        sameLine();
        if (button("Valider", {.style = ButtonStyle::Outline, .width = 100.0f, .disabled = m_answer[0] == '\0'})) {
            session.acceptAnswer(m_answer);
            m_answer[0] = '\0';
        }
        break;
    case State::InSalon:
        drawMembers(ctx, inner);
        break;
    }

    if (session.state() != State::Idle) {
        space(10.0f);
        const char* leave = session.state() == State::Hosting ? "Fermer le salon"
                          : session.state() == State::InSalon ? "Quitter le salon" : "Annuler";
        if (button(leave, {.style = ButtonStyle::Ghost, .icon = draw::Icon::Close, .height = 40.0f})) {
            session.leave();
            ctx.online.match.clear();
            m_answer[0] = '\0';
        }
    }
    endCard();
}

// --- Le jeu du salon -----------------------------------------------------------------------

void FriendsScreen::drawGame(UiContext& ctx, float width, float height)
{
    net::Session& session = ctx.session;
    beginCard("##salonGame", ImVec2(width, height), 22.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float inner = ImGui::GetContentRegionAvail().x;
    const auto& game = session.game();
    const bool host = session.isHost();
    const bool picking = !ctx.online.picking.empty();

    // Jaquette : la nôtre si on a le jeu, sinon un emplacement neutre.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float coverH = height - px(44.0f);
    const float coverW = coverH * 0.75f;
    const Game* local = ctx.online.match.empty() ? nullptr : ctx.library.find(ctx.online.match);
    if (local) {
        draw::shadow(dl, p, ImVec2(p.x + coverW, p.y + coverH), px(12.0f), px(10.0f), px(6.0f), 28);
        ctx.covers.draw(dl, p, ImVec2(p.x + coverW, p.y + coverH), *local, ctx.userData, px(12.0f), 14.0f);
    } else {
        draw::rect(dl, p, ImVec2(p.x + coverW, p.y + coverH), theme::Ground, px(12.0f));
        draw::icon(dl, draw::Icon::Gamepad, ImVec2(p.x + coverW / 2 - px(16.0f), p.y + coverH / 2 - px(16.0f)), px(32.0f),
                   theme::Track);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + coverW + px(22.0f), p.y));
    ImGui::BeginGroup();
    const float textW = inner - coverW - px(22.0f);
    overline("JEU DU SALON");
    if (!game) {
        text(picking ? "Calcul de l'empreinte du jeu…" : "Aucun jeu choisi", 22.0f, Weight::ExtraBold);
        textWrapped(host ? "Choisissez un jeu de votre bibliothèque : l'émulateur vérifiera que chacun a exactement le "
                           "même fichier."
                         : "L'hôte n'a pas encore choisi de jeu.",
                    13.0f, Weight::Medium, theme::Muted, textW);
    } else {
        // Titre et console.
        draw::textEllipsis(dl, ImGui::GetCursorScreenPos(), Weight::ExtraBold, 22.0f, theme::Ink, game->title, textW);
        ImGui::Dummy(ImVec2(textW, px(30.0f)));
        const System* system = Library::systemById(game->systemId);
        text(system ? system->name : game->systemId, 13.0f, Weight::SemiBold, theme::Muted);
        space(2.0f);

        // Qui est prêt ?
        int ready = 0;
        for (const net::Member& m : session.members())
            ready += m.status == GameStatus::Ready ? 1 : 0;
        const std::string readyText = std::to_string(ready) + " / " + std::to_string(session.members().size()) + " prêts";
        pill(readyText.c_str(), ready == static_cast<int>(session.members().size()) ? Tone::Success : Tone::Warning, 13.0f);
        sameLine(8.0f);
        const GameStatus mine = session.localStatus();
        std::string mineText = std::string("Vous : ") + statusLabel(mine);
        if (mine == GameStatus::Checking && ctx.online.hashProgress > 0.0f) {
            char pct[16];
            std::snprintf(pct, sizeof(pct), " %d %%", static_cast<int>(ctx.online.hashProgress * 100));
            mineText += pct;
        }
        pill(mineText.c_str(), toneOf(mine), 13.0f);
        if (game->link) {
            // Câble link : chacun son jeu de cette console.
            space(2.0f);
            const std::string console = system ? system->shortName : game->systemId;
            textWrapped(local ? "Votre jeu : " + local->title
                              : "Choisissez votre jeu " + console + " : le vôtre, avec votre sauvegarde.",
                        13.0f, Weight::SemiBold, local ? theme::Ink : theme::Warning, textW);
        } else if (mine == GameStatus::Missing || mine == GameStatus::Different)
            textWrapped(mine == GameStatus::Missing
                            ? "Ce jeu n'est pas dans votre bibliothèque. Ajoutez votre copie, de la même version que l'hôte."
                            : "Vous avez ce jeu, mais pas le même fichier (autre région ou version). Il faut exactement le même.",
                        12.0f, Weight::Medium, theme::Muted, textW);
    }
    // Une console portable (Game Boy, GBA, DS) se joue de deux façons : chacun
    // sa console reliée aux autres (le mode choisi d'office), ou une seule
    // console partagée. Les consoles de salon : toujours une console partagée.
    const net::LinkPlay::ConsoleLink* console = game ? net::LinkPlay::consoleLink(game->systemId) : nullptr;
    if (host && console) {
        space(6.0f);
        static const char* const modes[] = {"Chacun sa console", "Même console"};
        int mode = game->link ? 0 : 1;
        if (segmented("##mode", modes, &mode))
            ctx.setLinkMode(mode == 0);
    }
    // Chacun sa console : ce que ça permet, et le core nécessaire chez chacun.
    std::string missingCore;
    if (game && game->link && console) {
        std::error_code ec;
        if (!std::filesystem::exists(ctx.dir("cores") / console->coreFile, ec))
            missingCore = std::string("Ce mode demande le core ") + console->coreName + " (" + console->coreFile +
                          "), chez chaque joueur";
        space(2.0f);
        textWrapped(missingCore.empty() ? console->description : missingCore, 12.0f, Weight::Medium,
                    missingCore.empty() ? theme::Muted : theme::Warning, textW);
        if (!host) {
            if (button("Choisir mon jeu", {.style = ButtonStyle::Outline, .icon = draw::Icon::Gamepad, .height = 34.0f,
                                           .fontSize = 13.0f})) {
                m_pickOwn = true;
                m_pickNight = false;
                m_openPicker = true;
            }
        }
    }
    // Hôte, console partagée : d'où part la partie ? D'une partie neuve, ou de
    // sa propre progression (sa sauvegarde est envoyée à tous au départ).
    const char* missingSave = nullptr;
    if (host && game && local && !game->link) {
        space(6.0f);
        static const char* const starts[] = {"Nouvelle partie", "Ma progression", "Sauvegarde auto", "Sauvegarde F2"};
        segmented("##startFrom", starts, &ctx.online.startFrom);
        const std::filesystem::path files[] = {{}, gameSaveFile(ctx, *local),
                                               autoStateFile(ctx, *local), quickStateFile(ctx, *local)};
        const int start = ctx.online.startFrom;
        const char* explain = "Tout le monde part du début du jeu. Personne ne perd sa sauvegarde.";
        if (start == 1)
            explain = "Votre sauvegarde du jeu est chargée : vos amis jouent dans votre partie. Votre progression est "
                      "enregistrée à la fin.";
        else if (start >= 2)
            explain = "Reprise exacte de votre sauvegarde d'état. Votre progression est enregistrée à la fin.";
        if (start > 0 && fileTime(files[start]) == 0) {
            missingSave = start == 1 ? "Pas encore de sauvegarde pour ce jeu : jouez-y d'abord seul"
                                     : "Cette sauvegarde d'état n'existe pas pour ce jeu";
            explain = missingSave;
        }
        textWrapped(explain, 12.0f, Weight::Medium, missingSave ? theme::Warning : theme::Muted, textW);
    }
    space(4.0f);
    if (host) {
        if (button(game ? "Changer de jeu" : "Choisir le jeu", {.style = game ? ButtonStyle::Outline : ButtonStyle::Primary,
                                                                 .icon = draw::Icon::Gamepad, .height = 40.0f,
                                                                 .disabled = picking})) {
            m_pickOwn = false;
            m_pickNight = false;
            m_openPicker = true;
        }
        sameLine();
    }
    // Lancer : l'hôte seul, quand tout le monde a le bon fichier (4 joueurs au plus).
    const char* why = nullptr;
    int ready = 0;
    for (const net::Member& m : session.members())
        ready += m.status == GameStatus::Ready ? 1 : 0;
    if (!host)
        why = "C'est l'hôte qui lance la partie";
    else if (!game)
        why = "Choisissez d'abord un jeu";
    else if (session.members().size() < 2)
        why = "Attendez qu'un ami rejoigne le salon";
    else if (session.members().size() > net::kNetPlayers)
        why = "4 joueurs au plus pour une partie";
    else if (ready != static_cast<int>(session.members().size()))
        why = "Tout le monde doit être « Prêt »";
    else if (missingSave)
        why = missingSave;
    else if (!missingCore.empty())
        why = missingCore.c_str();
    if (button("Lancer la partie", {.style = ButtonStyle::Primary, .icon = draw::Icon::Play, .height = 40.0f,
                                    .disabled = why != nullptr, .tooltip = why}))
        ctx.startNetplay();
    // La connexion avec l'ami le moins bien relié : à voir avant de lancer.
    // (Un membre ne mesure lui-même que sa liaison avec l'hôte.)
    const net::Member* worst = nullptr;
    for (const net::Member& m : session.members()) {
        if (m.self || (!host && !m.host))
            continue;
        if (!worst || static_cast<int>(net::qualityOf(m.pingMs, m.lossPct)) >
                          static_cast<int>(net::qualityOf(worst->pingMs, worst->lossPct)))
            worst = &m;
    }
    if (worst) {
        const net::LinkQuality q = net::qualityOf(worst->pingMs, worst->lossPct);
        space(2.0f);
        const std::string label = std::string("Connexion : ") + net::qualityLabel(q);
        pill(label.c_str(), toneOf(q), 12.0f);
        if (worst->pingMs >= 0) {
            sameLine(8.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3.0f));
            text((session.members().size() > 2 ? worst->name + " : " : std::string()) + linkText(*worst), 12.0f,
                 Weight::Medium, theme::Muted);
        }
        if (q == net::LinkQuality::Poor)
            textWrapped("Connexion difficile : la partie risque de ramer. Un câble Ethernet aide souvent plus que le Wi-Fi.",
                        12.0f, Weight::Medium, theme::Warning, textW);
    }
    if (!host && game)
        text("La partie démarrera chez vous automatiquement.", 12.0f, Weight::Medium, theme::Muted);
    ImGui::EndGroup();
    endCard();
}

// --- Soirée jeu ------------------------------------------------------------------------------

void advanceGameNight(UiContext& ctx)
{
    OnlineState& o = ctx.online;
    if (!ctx.session.isHost() || o.night.empty())
        return;
    // Le jeu suivant encore présent dans la bibliothèque.
    const Game* next = nullptr;
    while (!next && ++o.nightIndex < static_cast<int>(o.night.size()))
        next = ctx.library.find(o.night[static_cast<size_t>(o.nightIndex)]);
    std::string message;
    if (!next) {
        o.nightIndex = -1; // (la liste reste, pour une autre soirée)
        message = "Fin de la soirée jeu, merci d'avoir joué !";
    } else {
        ctx.chooseSalonGame(*next);
        message = "Soirée jeu · jeu " + std::to_string(o.nightIndex + 1) + "/" + std::to_string(o.night.size()) + " : " +
                  next->title;
    }
    ctx.session.sendChat(message);
    ctx.online.chat.push_back({ctx.settings.nickname, message, true});
    ctx.notify(message);
}

void FriendsScreen::drawNight(UiContext& ctx, float width, float height)
{
    OnlineState& o = ctx.online;
    beginCard("##night", ImVec2(width, height), 20.0f);
    const float inner = ImGui::GetContentRegionAvail().x;
    overline("SOIRÉE JEU");
    const bool picking = !o.picking.empty();
    if (o.night.empty()) {
        // Le texte à gauche, le bouton à droite sur la même hauteur.
        const ImVec2 start = ImGui::GetCursorPos();
        textWrapped("Préparez une liste de jeux : à la fin de chaque partie, le suivant est choisi pour tout le salon.",
                    13.0f, Weight::Medium, theme::Muted, inner - px(210.0f));
        ImGui::SetCursorPos(ImVec2(start.x + inner - px(190.0f), start.y));
        if (button("Préparer la soirée", {.style = ButtonStyle::Outline, .icon = draw::Icon::List, .height = 40.0f,
                                          .width = 190.0f})) {
            m_pickNight = true;
            m_pickOwn = false;
            m_openPicker = true;
        }
        endCard();
        return;
    }
    // La liste en une ligne : le jeu en cours en gras, les suivants à la suite.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int count = static_cast<int>(o.night.size());
    const bool started = o.nightIndex >= 0;
    std::string line;
    for (int i = std::max(0, o.nightIndex); i < count; ++i) {
        const Game* g = ctx.library.find(o.night[static_cast<size_t>(i)]);
        line += (line.empty() ? "" : "  ›  ") + (g ? g->title : toUtf8(o.night[static_cast<size_t>(i)].stem()));
    }
    const std::string status = started ? "Jeu " + std::to_string(o.nightIndex + 1) + " sur " + std::to_string(count)
                                       : std::to_string(count) + (count > 1 ? " jeux prévus" : " jeu prévu");
    // À gauche : où en est la soirée ; à droite : les boutons.
    const float buttonsW = px(190.0f + 8.0f + 38.0f + 8.0f + 38.0f);
    const ImVec2 start = ImGui::GetCursorPos();
    text(status, 16.0f, Weight::ExtraBold);
    draw::textEllipsis(dl, ImGui::GetCursorScreenPos(), Weight::Medium, 13.0f, theme::Muted, line, inner - buttonsW - px(16.0f));
    ImGui::SetCursorPos(ImVec2(start.x + inner - buttonsW, start.y + px(2.0f)));
    const bool last = o.nightIndex + 1 >= count;
    if (button(started ? (last ? "Terminer la soirée" : "Jeu suivant") : "Commencer la soirée",
               {.style = started ? ButtonStyle::Outline : ButtonStyle::Primary,
                .icon = started ? draw::Icon::ChevronRight : draw::Icon::Play, .height = 38.0f, .width = 190.0f,
                .fontSize = 13.0f, .disabled = picking}))
        advanceGameNight(ctx);
    sameLine(8.0f);
    if (iconButton("##editNight", draw::Icon::Pencil, ButtonStyle::Soft, 38.0f, "Modifier la liste")) {
        m_pickNight = true;
        m_pickOwn = false;
        m_openPicker = true;
    }
    sameLine(8.0f);
    if (iconButton("##clearNight", draw::Icon::Close, ButtonStyle::Soft, 38.0f, "Vider")) {
        o.night.clear();
        o.nightIndex = -1;
    }
    endCard();
}

void FriendsScreen::drawGamePicker(UiContext& ctx)
{
    if (m_openPicker) {
        ImGui::OpenPopup("##gamePicker");
        m_openPicker = false;
        m_pickerSearch[0] = '\0';
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(620.0f), px(620.0f)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(28.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopupModal("##gamePicker", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 top = ImGui::GetCursorPos();
        // Deux usages : l'hôte choisit le jeu du salon, ou (câble link)
        // chacun choisit SON jeu de la console du salon.
        const auto& salonGame = ctx.session.game();
        const bool own = m_pickOwn && salonGame;
        const bool night = m_pickNight && !own;
        text(night ? "Préparer la soirée jeu" : own ? "Choisir votre jeu" : "Choisir le jeu du salon", 22.0f, Weight::ExtraBold);
        text(night ? "Cliquez les jeux dans l'ordre où vous voulez y jouer."
                   : own ? "Le vôtre, avec votre sauvegarde : il sera relié par le câble à celui de vos amis."
                         : "Vos amis devront avoir exactement le même fichier.",
             13.0f, Weight::Medium, theme::Muted);
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(top.x + width - px(night ? 110.0f : 40.0f), top.y));
        if (night) {
            if (button("Terminé", {.style = ButtonStyle::Primary, .height = 40.0f, .width = 110.0f}))
                ImGui::CloseCurrentPopup();
        } else if (iconButton("##close", draw::Icon::Close, ButtonStyle::Soft, 40.0f))
            ImGui::CloseCurrentPopup();
        ImGui::SetCursorPos(after);
        space(4.0f);
        searchField("##search", m_pickerSearch, sizeof(m_pickerSearch), "Rechercher", width / theme::scale());
        space(4.0f);

        ImGui::BeginChild("##list", ImVec2(0, 0));
        const std::string needle = lower(m_pickerSearch);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowW = ImGui::GetContentRegionAvail().x;
        for (const Game& g : ctx.library.games()) {
            if (!needle.empty() && lower(g.title).find(needle) == std::string::npos)
                continue;
            if (own && g.system->id != salonGame->systemId)
                continue;
            if (g.system->external())
                continue; // (émulateur externe : pas de jeu en ligne)
            ImGui::PushID(&g);
            const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, px(64.0f)));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 p = ImGui::GetItemRectMin();
            if (hovered)
                draw::rect(dl, p, ImVec2(p.x + rowW, p.y + px(64.0f)), theme::Ground, px(12.0f));
            ctx.covers.draw(dl, ImVec2(p.x + px(8.0f), p.y + px(6.0f)), ImVec2(p.x + px(47.0f), p.y + px(58.0f)), g,
                            ctx.userData, px(6.0f), 0.0f);
            draw::textEllipsis(dl, ImVec2(p.x + px(60.0f), p.y + px(13.0f)), Weight::Bold, 14.0f, theme::Ink, g.title,
                               rowW - px(80.0f));
            draw::text(dl, ImVec2(p.x + px(60.0f), p.y + px(34.0f)), Weight::Medium, 12.0f, theme::Muted,
                       std::string(g.system->name) + " · " + formatSize(g.size));
            // Soirée jeu : le numéro du jeu dans la liste, à droite.
            auto& list = ctx.online.night;
            const auto inList = std::find(list.begin(), list.end(), g.path);
            if (night && inList != list.end()) {
                const ImVec2 c(p.x + rowW - px(30.0f), p.y + px(32.0f));
                dl->AddCircleFilled(c, px(14.0f), theme::Accent, 24);
                const std::string number = std::to_string(inList - list.begin() + 1);
                const ImVec2 ns = draw::measure(Weight::ExtraBold, 13.0f, number);
                draw::text(dl, ImVec2(c.x - ns.x / 2, c.y - ns.y / 2), Weight::ExtraBold, 13.0f, theme::White, number);
            }
            if (clicked && night) {
                // Un clic ajoute le jeu en fin de liste, un second le retire.
                if (inList != list.end()) {
                    const int removed = static_cast<int>(inList - list.begin());
                    list.erase(inList);
                    if (ctx.online.nightIndex >= removed && ctx.online.nightIndex > 0)
                        --ctx.online.nightIndex;
                } else {
                    list.push_back(g.path);
                }
            } else if (clicked) {
                if (own)
                    ctx.online.linkChoice = g.path;
                else
                    ctx.chooseSalonGame(g);
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

// --- Discussion ----------------------------------------------------------------------------

void FriendsScreen::drawChat(UiContext& ctx, float width, float height)
{
    beginCard("##chat", ImVec2(width, height), 22.0f);
    const float inner = ImGui::GetContentRegionAvail().x;
    text("Discussion", 18.0f, Weight::ExtraBold);
    space(2.0f);

    // Messages, dans une zone qui défile.
    const float inputH = px(52.0f);
    ImGui::BeginChild("##messages", ImVec2(inner, ImGui::GetContentRegionAvail().y - inputH));
    for (const ChatLine& line : ctx.online.chat) {
        if (line.author.empty()) {
            text(line.text, 12.0f, Weight::SemiBold, theme::Muted);
            continue;
        }
        text(line.author, 12.0f, Weight::Bold, line.mine ? theme::Accent : theme::Ink);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8.0f));
        textWrapped(line.text, 14.0f, Weight::Medium, theme::Body, inner - px(12.0f));
        space(2.0f);
    }
    if (m_chatSeen != ctx.online.chat.size()) {
        ImGui::SetScrollHereY(1.0f);
        m_chatSeen = ctx.online.chat.size();
    }
    ImGui::EndChild();

    // Saisie : Entrée envoie.
    const bool connected = ctx.session.inSalon();
    textField("##message", m_message, sizeof(m_message), connected ? "Écrire un message…" : "Pas encore connecté",
              (inner - px(110.0f)) / theme::scale(), !connected);
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    sameLine();
    const bool clicked = button("Envoyer", {.style = ButtonStyle::Primary, .width = 100.0f, .disabled = !connected});
    if ((enter || clicked) && connected && m_message[0] != '\0') {
        ctx.session.sendChat(m_message);
        ctx.online.chat.push_back({ctx.settings.nickname, m_message, true});
        m_message[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    endCard();
}

void FriendsScreen::drawHelp(float width)
{
    beginCard("##how", ImVec2(width, px(330.0f)), 24.0f);
    const float inner = ImGui::GetContentRegionAvail().x;
    text("Comment ça marche", 18.0f, Weight::ExtraBold);
    space(4.0f);
    struct Step {
        const char* title;
        const char* body;
    };
    const Step steps[] = {
        {"1. Créez un salon", "Donnez-lui un nom et, si vous voulez, un mot de passe. Votre émulateur trouve votre adresse "
                              "sur Internet et demande à votre box d'ouvrir un port."},
        {"2. Partagez le code", "Le code contient tout ce qu'il faut pour vous joindre, jusqu'à 7 amis. Aucun compte."},
        {"3. Choisissez le jeu", "Chacun vérifie automatiquement qu'il a exactement le même fichier que vous."},
    };
    for (const Step& s : steps) {
        text(s.title, 14.0f, Weight::Bold);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        textWrapped(s.body, 13.0f, Weight::Medium, theme::Muted, inner);
        space(2.0f);
    }
    endCard();
    space(14.0f);

    beginCard("##vpn", ImVec2(width, px(110.0f)), 20.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ip = ImGui::GetCursorScreenPos();
    draw::rect(dl, ip, ImVec2(ip.x + px(44.0f), ip.y + px(44.0f)), theme::AccentSoft, px(14.0f));
    draw::icon(dl, draw::Icon::Gamepad, ImVec2(ip.x + px(11.0f), ip.y + px(11.0f)), px(22.0f), theme::Accent);
    ImGui::SetCursorScreenPos(ImVec2(ip.x + px(60.0f), ip.y + px(2.0f)));
    ImGui::BeginGroup();
    text("Ça ne passe toujours pas ?", 14.0f, Weight::ExtraBold);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    textWrapped("Utilisez Radmin VPN, Hamachi ou Tailscale : l'émulateur passe directement par eux, sans réglage (avec "
                "Tailscale, donnez le code complet).",
                13.0f, Weight::Medium, theme::Muted, width - px(100.0f));
    ImGui::EndGroup();
    endCard();
}

void FriendsScreen::draw(UiContext& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float leftW = px(440.0f);
    const float rightW = avail.x - leftW - px(24.0f);

    drawSalon(ctx, leftW, avail.y);

    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginGroup();
    text("Jouer en ligne", 30.0f, Weight::ExtraBold);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    text("Sans compte ni serveur : vos émulateurs se parlent directement.", 14.0f, Weight::Medium, theme::Muted);
    space(10.0f);
    if (ctx.session.inSalon()) {
        // (L'hôte a une ligne de plus : le point de départ de la partie.)
        const auto& salonGame = ctx.session.game();
        const bool linkable = salonGame && net::LinkPlay::consoleLink(salonGame->systemId);
        float gameH = 264.0f; // (+ la ligne "Connexion")
        if (ctx.session.isHost() && salonGame)
            gameH = linkable ? 424.0f : 334.0f;
        else if (salonGame && salonGame->link)
            gameH = 394.0f;
        drawGame(ctx, rightW, px(gameH));
        space(14.0f);
        if (ctx.session.isHost()) {
            drawNight(ctx, rightW, px(ctx.online.night.empty() ? 112.0f : 118.0f));
            space(14.0f);
        }
        drawChat(ctx, rightW, ImGui::GetContentRegionAvail().y);
    } else if (!ctx.online.chat.empty()) {
        drawChat(ctx, rightW, ImGui::GetContentRegionAvail().y); // la discussion reste lisible après la fin du salon
    } else {
        drawHelp(rightW);
    }
    ImGui::EndGroup();
    drawGamePicker(ctx);
}

} // namespace ui
