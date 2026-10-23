// Morceaux partagés par plusieurs écrans : mise en forme des dates et
// tailles, titres de page, tuiles de jaquette.

#include "../SaveFiles.hpp"
#include "../Utf8.hpp"
#include "Screens.hpp"
#include "Widgets.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>

namespace fs = std::filesystem;
using theme::px;
using theme::Weight;

namespace ui {

const char* statusLabel(net::GameStatus status)
{
    switch (status) {
    case net::GameStatus::Unknown: return "En attente";
    case net::GameStatus::Checking: return "Vérification…";
    case net::GameStatus::Ready: return "Prêt";
    case net::GameStatus::Missing: return "Jeu introuvable";
    case net::GameStatus::Different: return "Autre version";
    }
    return "";
}

// Les sauvegardes d'un jeu sont dans son dossier : saves/<nom>/ (voir SaveFiles.hpp).
fs::path gameSaveDir(const UiContext& ctx, const Game& game)
{
    return saveFilesFor(ctx.dir("saves"), toUtf8(game.path.stem())).dir;
}

fs::path quickStateFile(const UiContext& ctx, const Game& game)
{
    return saveFilesFor(ctx.dir("saves"), toUtf8(game.path.stem())).state;
}

fs::path gameSaveFile(const UiContext& ctx, const Game& game)
{
    return saveFilesFor(ctx.dir("saves"), toUtf8(game.path.stem())).sram;
}

fs::path cheatsFile(const UiContext& ctx, const Game& game)
{
    return ctx.dir("cheats") / fromUtf8(toUtf8(game.path.stem()) + ".txt");
}

fs::path autoStateFile(const UiContext& ctx, const Game& game)
{
    return saveFilesFor(ctx.dir("saves"), toUtf8(game.path.stem())).autoState;
}

void openFolder(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    SDL_OpenURL(("file:///" + toUtf8(dir)).c_str());
}

std::string formatAgo(long long timestamp)
{
    if (timestamp <= 0)
        return "Jamais joué";
    const long long elapsed = static_cast<long long>(std::time(nullptr)) - timestamp;
    if (elapsed < 60)
        return "À l'instant";
    if (elapsed < 3600)
        return "Il y a " + std::to_string(elapsed / 60) + " min";
    if (elapsed < 86400)
        return "Il y a " + std::to_string(elapsed / 3600) + " h";
    const long long days = elapsed / 86400;
    if (days == 1)
        return "Hier";
    if (days < 7)
        return "Il y a " + std::to_string(days) + " jours";
    if (days < 14)
        return "La semaine dernière";
    if (days < 31)
        return "Il y a " + std::to_string(days / 7) + " semaines";
    if (days < 365)
        return "Il y a " + std::to_string(days / 30) + " mois";
    return "Il y a plus d'un an";
}

std::string formatDuration(long long seconds)
{
    if (seconds <= 0)
        return "0 min";
    if (seconds < 3600)
        return std::to_string(std::max(1LL, seconds / 60)) + " min";
    return std::to_string(seconds / 3600) + " h";
}

std::string formatSize(std::uintmax_t bytes)
{
    char text[32];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        std::snprintf(text, sizeof(text), "%.1f Go", mb / 1024.0);
    else if (mb >= 1.0)
        std::snprintf(text, sizeof(text), "%.1f Mo", mb);
    else
        std::snprintf(text, sizeof(text), "%.0f Ko", static_cast<double>(bytes) / 1024.0);
    // Virgule décimale à la française.
    for (char* c = text; *c; ++c)
        if (*c == '.')
            *c = ',';
    return text;
}

long long fileTime(const fs::path& file)
{
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec)
        return 0;
    // [C++20] Conversion de l'horloge des fichiers vers l'horloge système.
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

void pageTitle(const char* title, const std::string& subtitle)
{
    text(title, 34.0f, Weight::ExtraBold);
    if (!subtitle.empty()) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6.0f));
        text(subtitle, 14.0f, Weight::Medium, theme::Muted);
    }
}

bool sectionHeader(const char* title, const char* linkLabel, float width)
{
    const ImVec2 start = ImGui::GetCursorPos();
    text(title, 20.0f, Weight::ExtraBold);
    if (!linkLabel)
        return false;
    const ImVec2 after = ImGui::GetCursorPos();
    const float linkWidth = draw::measure(Weight::Bold, 14.0f, linkLabel).x;
    ImGui::SetCursorPos(ImVec2(start.x + width - linkWidth, start.y + px(5.0f)));
    const bool clicked = link(linkLabel);
    ImGui::SetCursorPos(after);
    return clicked;
}

TileResult coverTile(const UiContext& ctx, const Game& game, float width, float height, const std::string& subtitle,
                     float titleSize)
{
    TileResult result;
    const float textBlock = px(42.0f);
    ImGui::PushID(&game);
    result.clicked = ImGui::InvisibleButton("##tile", ImVec2(width, height + px(10.0f) + textBlock));
    const bool hovered = ImGui::IsItemHovered();
    result.doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // Au survol, la jaquette monte de 4 px et son ombre grandit.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetItemID();
    float lift = storage->GetFloat(key, 0.0f);
    lift += ((hovered ? 1.0f : 0.0f) - lift) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    storage->SetFloat(key, lift);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetItemRectMin();
    const ImVec2 min(pos.x, pos.y - px(4.0f) * lift);
    const ImVec2 max(min.x + width, min.y + height);
    const float rounding = px(14.0f);
    draw::shadow(dl, min, max, rounding, px(10.0f + 8.0f * lift), px(6.0f + 4.0f * lift), 26 + static_cast<int>(14 * lift));
    ctx.covers.draw(dl, min, max, game, ctx.userData, rounding, titleSize);

    const float y = pos.y + height + px(10.0f);
    draw::textEllipsis(dl, ImVec2(pos.x, y), Weight::Bold, 14.0f, theme::Ink, game.title, width);
    draw::textEllipsis(dl, ImVec2(pos.x, y + px(20.0f)), Weight::Medium, 12.0f, theme::Muted, subtitle, width);
    return result;
}

} // namespace ui
