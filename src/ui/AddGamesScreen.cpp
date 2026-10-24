// Ajouter des jeux : zone de dépôt, analyse des fichiers ajoutés (console
// reconnue, à vérifier, non prise en charge), dossiers surveillés, scraping.

#include "../Archive.hpp"
#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

namespace {

std::string lowerExtension(const fs::path& file)
{
    std::string ext = toUtf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// Un fichier qui ressemble à un jeu de console (pas à un morceau de jeu
// d'arcade, dont les fichiers ont des extensions quelconques).
bool looksLikeGame(const fs::path& file)
{
    static const char* const discs[] = {".cue", ".chd", ".iso", ".gdi", ".cdi", ".m3u", ".rvz", ".cso", ".pbp", ".wbfs"};
    const std::string ext = lowerExtension(file);
    if (std::find(std::begin(discs), std::end(discs), ext) != std::end(discs))
        return true;
    const System* system = Library::guessSystem(file);
    return system && std::string_view(system->id) != "arcade" && std::string_view(system->id) != "neogeo";
}

} // namespace

// Un .zip (ou .7z) qui contient des jeux de console : une compilation
// ("Tous les jeux GBA.zip"), ou une seule cartouche compressée. Sans ça, il
// passerait pour un jeu d'arcade (les jeux d'arcade SONT des .zip).
// On décompresse, puis chaque jeu va dans roms/<console>/. Renvoie false si
// ce n'est pas une archive de jeux (un vrai jeu d'arcade, par exemple).
bool AddGamesScreen::importPack(UiContext& ctx, const fs::path& archive)
{
    const std::string ext = lowerExtension(archive);
    if (ext != ".zip" && ext != ".7z")
        return false;
    const std::vector<fs::path> content = archive::list(archive);
    if (std::none_of(content.begin(), content.end(), looksLikeGame))
        return false;

    // Décompressé dans imports/<nom de l'archive>/ : les jeux dont la
    // console reste à choisir y attendent.
    std::error_code ec;
    const fs::path work = ctx.dir("imports") / archive.stem();
    fs::remove_all(work, ec);
    if (!archive::extract(archive, work)) {
        ctx.notify("Impossible de décompresser " + toUtf8(archive.filename()));
        return true;
    }
    // Les pistes d'un .cue (ou les disques d'un .m3u) suivent leur jeu : ce
    // ne sont pas des jeux à part.
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(work, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    std::set<fs::path> parts;
    for (const fs::path& f : files) {
        const std::vector<fs::path> all = Library::filesOf(f);
        for (size_t i = 1; i < all.size(); ++i)
            parts.insert(all[i].lexically_normal());
    }
    int added = 0;
    for (const fs::path& f : files) {
        if (parts.count(f.lexically_normal()) || !looksLikeGame(f))
            continue;
        ImportItem item;
        item.title = cleanTitle(toUtf8(f.stem()));
        // (Les dossiers de l'archive aident à deviner : "PS2/Jeu.iso".)
        if (const System* system = Library::guessSystem(f)) {
            const fs::path dest = ctx.library.romsDir() / system->id;
            fs::create_directories(dest, ec);
            for (const fs::path& part : Library::filesOf(f)) {
                std::error_code moveEc;
                fs::rename(part, dest / part.filename(), moveEc);
                if (moveEc) { // (autre disque : copie, puis suppression)
                    fs::copy_file(part, dest / part.filename(), fs::copy_options::overwrite_existing, moveEc);
                    fs::remove(part, moveEc);
                }
            }
            item.path = dest / f.filename();
            item.system = system;
            item.status = ImportItem::Status::Added;
            ++added;
        } else {
            item.path = f;
            item.choices = Library::candidatesFor(f);
            item.status = item.choices.empty() ? ImportItem::Status::Unsupported : ImportItem::Status::Ambiguous;
        }
        m_items.insert(m_items.begin(), std::move(item));
    }
    // Plus rien à choisir dans le dossier de travail : on l'efface.
    bool waiting = false;
    for (const ImportItem& item : m_items)
        waiting = waiting || (item.status == ImportItem::Status::Ambiguous && item.path.parent_path() == work);
    if (!waiting)
        fs::remove_all(work, ec);
    ctx.notify(std::to_string(added) + (added > 1 ? " jeux ajoutés depuis " : " jeu ajouté depuis ") +
               toUtf8(archive.filename()));
    return true;
}

void AddGamesScreen::import(UiContext& ctx, const std::vector<fs::path>& paths)
{
    std::error_code ec;
    for (const fs::path& path : paths) {
        if (!fs::is_directory(path, ec) && importPack(ctx, path))
            continue;
        ImportItem item;
        item.path = path;
        item.title = cleanTitle(toUtf8(path.stem()));
        if (fs::is_directory(path, ec)) {
            // Un dossier devient un dossier surveillé.
            ctx.userData.addWatchedFolder(path);
            item.title = toUtf8(path.filename());
            item.status = ImportItem::Status::Folder;
        } else if (const System* system = Library::guessSystem(path)) {
            ctx.userData.addManualGame(path, system->id);
            item.system = system;
            item.status = ImportItem::Status::Added;
        } else {
            item.choices = Library::candidatesFor(path);
            item.status = item.choices.empty() ? ImportItem::Status::Unsupported : ImportItem::Status::Ambiguous;
        }
        // Les plus récents en haut de la liste.
        m_items.insert(m_items.begin(), std::move(item));
    }
    ctx.userData.save();
    ctx.rescan();
}

void AddGamesScreen::draw(UiContext& ctx)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float asideW = px(380.0f);
    const float leftW = avail.x - asideW - px(24.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::BeginGroup();
    pageTitle("Ajouter des jeux",
              "La console de chaque jeu est reconnue automatiquement. Les informations en ligne arriveront avec le scraping.");
    space(10.0f);

    // --- Zone de dépôt en pointillés.
    const ImVec2 zoneMin = ImGui::GetCursorScreenPos();
    const ImVec2 zoneMax(zoneMin.x + leftW, zoneMin.y + px(230.0f));
    draw::rect(dl, zoneMin, zoneMax, theme::SurfaceAlt, px(24.0f));
    draw::dashedRect(dl, zoneMin, zoneMax, theme::Dashed, px(24.0f), px(2.0f));
    const float cx = (zoneMin.x + zoneMax.x) / 2;
    dl->AddCircleFilled(ImVec2(cx, zoneMin.y + px(58.0f)), px(28.0f), theme::AccentSoft, 32);
    draw::icon(dl, draw::Icon::Upload, ImVec2(cx - px(12.0f), zoneMin.y + px(46.0f)), px(24.0f), theme::Accent);
    const char* title = "Glissez vos jeux ici";
    const char* hint = "Fichiers ou dossiers entiers · ISO, CHD, CUE, SFC, Z64, GBA, NDS…";
    const ImVec2 ts = draw::measure(Weight::ExtraBold, 18.0f, title);
    const ImVec2 hs = draw::measure(Weight::Medium, 13.0f, hint);
    draw::text(dl, ImVec2(cx - ts.x / 2, zoneMin.y + px(100.0f)), Weight::ExtraBold, 18.0f, theme::Ink, title);
    draw::text(dl, ImVec2(cx - hs.x / 2, zoneMin.y + px(126.0f)), Weight::Medium, 13.0f, theme::Muted, hint);
    const float b1 = draw::measure(Weight::Bold, 14.0f, "Choisir des fichiers").x + px(36.0f);
    const float b2 = draw::measure(Weight::Bold, 14.0f, "Ajouter un dossier").x + px(36.0f);
    ImGui::SetCursorScreenPos(ImVec2(cx - (b1 + b2 + px(10.0f)) / 2, zoneMin.y + px(160.0f)));
    if (button("Choisir des fichiers", {.style = ButtonStyle::Primary, .height = 42.0f}))
        ctx.openDialog(DialogPurpose::AddGameFiles);
    sameLine();
    if (button("Ajouter un dossier", {.style = ButtonStyle::Outline, .height = 42.0f}))
        ctx.openDialog(DialogPurpose::AddGameFolder);
    ImGui::SetCursorScreenPos(ImVec2(zoneMin.x, zoneMax.y));
    space(20.0f);

    // --- Derniers ajouts.
    const float listH = ImGui::GetContentRegionAvail().y;
    beginCard("##imports", ImVec2(leftW, listH), 22.0f, theme::Surface, 24.0f, true);
    text("Derniers ajouts", 18.0f, Weight::ExtraBold);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    text(m_items.empty() ? "Les fichiers que vous ajoutez apparaîtront ici." : std::to_string(m_items.size()) + " élément(s) cette session",
         13.0f, Weight::Medium, theme::Muted);
    space(4.0f);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < m_items.size(); ++i) {
        ImportItem& item = m_items[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 row = ImGui::GetCursorScreenPos();
        const float rowH = px(68.0f);
        cdl->AddLine(ImVec2(row.x, row.y), ImVec2(row.x + rowW, row.y), theme::Divider, px(1.0f));
        // Vignette aux couleurs de la console.
        const ImU32 thumb = item.system ? theme::rgb(item.system->color) : theme::Track;
        draw::rect(cdl, ImVec2(row.x, row.y + px(10.0f)), ImVec2(row.x + px(36.0f), row.y + px(58.0f)), thumb, px(6.0f));
        if (item.status == ImportItem::Status::Folder)
            draw::icon(cdl, draw::Icon::Folder, ImVec2(row.x + px(8.0f), row.y + px(24.0f)), px(20.0f), theme::White);
        const float textW = rowW * 0.42f;
        draw::textEllipsis(cdl, ImVec2(row.x + px(50.0f), row.y + px(16.0f)), Weight::Bold, 14.0f, theme::Ink, item.title, textW);
        draw::textEllipsis(cdl, ImVec2(row.x + px(50.0f), row.y + px(36.0f)), Weight::Medium, 12.0f, theme::Muted,
                           toUtf8(item.path.filename()), textW);

        const float colX = row.x + px(50.0f) + textW + px(16.0f);
        const char* status = "Ajouté";
        Tone tone = Tone::Success;
        switch (item.status) {
        case ImportItem::Status::Added: break;
        case ImportItem::Status::Folder: status = "Dossier surveillé"; tone = Tone::Accent; break;
        case ImportItem::Status::Ambiguous: status = "À vérifier"; tone = Tone::Warning; break;
        case ImportItem::Status::Unsupported: status = "Non pris en charge"; tone = Tone::Danger; break;
        }
        if (item.system)
            draw::text(cdl, ImVec2(colX, row.y + px(25.0f)), Weight::SemiBold, 13.0f, theme::Body, item.system->name);
        pillAt(cdl, ImVec2(colX + px(150.0f), row.y + px(22.0f)), status, tone);

        // Console impossible à deviner : l'utilisateur choisit.
        if (item.status == ImportItem::Status::Ambiguous) {
            std::vector<const char*> names;
            for (const System* s : item.choices)
                names.push_back(s->name);
            ImGui::SetCursorScreenPos(ImVec2(row.x + rowW - px(300.0f), row.y + px(12.0f)));
            combo("##choice", names, &item.choice, 180.0f);
            sameLine(8.0f);
            if (button("Valider", {.style = ButtonStyle::Primary, .height = 40.0f})) {
                item.system = item.choices[item.choice];
                item.status = ImportItem::Status::Added;
                ctx.userData.addManualGame(item.path, item.system->id);
                ctx.userData.save();
                ctx.rescan();
            }
        }
        ImGui::SetCursorScreenPos(row);
        ImGui::Dummy(ImVec2(rowW, rowH - ImGui::GetStyle().ItemSpacing.y));
        ImGui::PopID();
    }
    endCard();
    ImGui::EndGroup();

    // --- Colonne de droite.
    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginGroup();
    space(70.0f);
    const auto& folders = ctx.userData.watchedFolders();
    const float foldersH = px(150.0f) + px(64.0f) * static_cast<float>(folders.size() + 1);
    beginCard("##folders", ImVec2(asideW, foldersH), 22.0f);
    text("Dossiers surveillés", 18.0f, Weight::ExtraBold);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    text("Les nouveaux jeux y sont détectés tout seuls.", 13.0f, Weight::Medium, theme::Muted);
    space(4.0f);
    const float innerW = ImGui::GetContentRegionAvail().x;
    auto folderRow = [&](const fs::path& folder, const std::string& label, int games, bool removable) {
        ImDrawList* fdl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw::rect(fdl, p, ImVec2(p.x + innerW, p.y + px(56.0f)), theme::Ground, px(14.0f));
        // Un dossier réseau (NAS) a son icône ; injoignable, il le dit.
        const bool network = Library::isNetworkPath(folder);
        const bool offline = ctx.library.isOffline(folder);
        draw::icon(fdl, draw::Icon::Folder, ImVec2(p.x + px(12.0f), p.y + px(18.0f)), px(20.0f),
                   offline ? theme::Warning : network ? theme::Accent : theme::Muted);
        draw::textEllipsis(fdl, ImVec2(p.x + px(44.0f), p.y + px(10.0f)), Weight::Bold, 13.0f, theme::Ink, label,
                           innerW - px(100.0f));
        if (offline)
            draw::text(fdl, ImVec2(p.x + px(44.0f), p.y + px(30.0f)), Weight::SemiBold, 12.0f, theme::Warning,
                       network ? "Injoignable : NAS éteint ou réseau coupé ?" : "Introuvable : disque débranché ?");
        else
            draw::text(fdl, ImVec2(p.x + px(44.0f), p.y + px(30.0f)), Weight::Medium, 12.0f, theme::Muted,
                       std::to_string(games) + (games > 1 ? " jeux" : " jeu") + (network ? " · réseau" : ""));
        ImGui::SetCursorScreenPos(ImVec2(p.x + innerW - px(42.0f), p.y + px(12.0f)));
        bool remove = false;
        if (removable)
            remove = iconButton("##remove", draw::Icon::Close, ButtonStyle::Ghost, 32.0f, "Ne plus surveiller ce dossier");
        else if (iconButton("##open", draw::Icon::Folder, ButtonStyle::Ghost, 32.0f, "Ouvrir le dossier"))
            openFolder(folder);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(64.0f)));
        return remove;
    };
    auto gamesIn = [&](const fs::path& folder) {
        const std::string prefix = UserData::keyOf(folder);
        return static_cast<int>(std::count_if(ctx.library.games().begin(), ctx.library.games().end(), [&](const Game& g) {
            return UserData::keyOf(g.path).rfind(prefix, 0) == 0;
        }));
    };
    ImGui::PushID("roms");
    folderRow(ctx.library.romsDir(), "roms (dossier de l'émulateur)", gamesIn(ctx.library.romsDir()), false);
    ImGui::PopID();
    fs::path toRemove;
    for (const fs::path& folder : folders) {
        ImGui::PushID(toUtf8(folder).c_str());
        if (folderRow(folder, toUtf8(folder), gamesIn(folder), true))
            toRemove = folder;
        ImGui::PopID();
    }
    if (!toRemove.empty()) {
        ctx.userData.removeWatchedFolder(toRemove);
        ctx.userData.save();
        ctx.rescan();
    }
    if (button("Ajouter un dossier surveillé", {.style = ButtonStyle::Outline, .height = 42.0f, .width = -1.0f}))
        ctx.openDialog(DialogPurpose::AddGameFolder);
    endCard();
    space(20.0f);

    // Un dossier sur le réseau (NAS, partage Windows) : on tape son chemin,
    // la fenêtre de choix de dossier ne montre pas toujours le réseau.
    std::error_code ec;
    beginCard("##network", ImVec2(asideW, px(230.0f)), 22.0f);
    text("Jeux sur un NAS", 18.0f, Weight::ExtraBold);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
    textWrapped("Un partage réseau devient un dossier surveillé : vos jeux restent sur le NAS.", 13.0f, Weight::Medium,
                theme::Muted, innerW);
    space(4.0f);
    textField("##networkPath", m_networkPath, sizeof(m_networkPath), "\\\\NAS\\Jeux", innerW / theme::scale());
    space(2.0f);
    if (button("Ajouter le dossier réseau", {.style = ButtonStyle::Outline, .height = 42.0f, .width = -1.0f,
                                             .disabled = m_networkPath[0] == '\0'})) {
        const fs::path folder = fromUtf8(m_networkPath);
        if (fs::is_directory(folder, ec)) {
            ctx.userData.addWatchedFolder(folder);
            ctx.userData.save();
            ctx.rescan();
            ctx.notify("Dossier réseau ajouté : " + toUtf8(folder));
            m_networkPath[0] = '\0';
        } else {
            ctx.notify("Dossier introuvable : vérifiez le chemin, et que le NAS est allumé");
        }
    }
    endCard();
    ImGui::EndGroup();
}

} // namespace ui
