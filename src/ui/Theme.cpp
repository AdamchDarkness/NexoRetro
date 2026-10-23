#include "Theme.hpp"

#include "../Utf8.hpp"

#include <SDL3/SDL.h>
#include <imgui_internal.h> // ImTextCharFromUtf8

#include <array>

namespace theme {

namespace {
float g_scale = 1.0f;
std::array<ImFont*, 4> g_fonts{};

ImVec4 toVec4(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}
} // namespace

ImU32 withAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

bool loadFonts(const std::filesystem::path& fontDir)
{
    ImGuiIO& io = ImGui::GetIO();
    // Les lettres sont rangées côte à côte dans une texture. Un peu plus
    // d'espace entre elles évite qu'un bout de la lettre voisine "bave"
    // quand le texte est placé entre deux pixels.
    io.Fonts->TexGlyphPadding = 2;
    // Quand la texture des lettres est pleine, ImGui jette les tailles non
    // utilisées depuis 2 images et RÉORGANISE la texture sur place : le texte
    // déjà dessiné dans l'image en cours montre alors des lettres au hasard
    // ("hardwa:e"). Toutes nos tailles préparées tiennent dans 2048 x 2048 :
    // on réserve le double pour ne jamais la remplir.
    io.Fonts->TexMinWidth = 4096;
    io.Fonts->TexMinHeight = 2048;
    const char* files[] = {"PlusJakartaSans-Medium.ttf", "PlusJakartaSans-SemiBold.ttf", "PlusJakartaSans-Bold.ttf",
                           "PlusJakartaSans-ExtraBold.ttf"};
    bool ok = true;
    for (size_t i = 0; i < g_fonts.size(); ++i) {
        const std::string path = toUtf8(fontDir / files[i]);
        g_fonts[i] = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f);
        if (!g_fonts[i]) {
            SDL_Log("Police introuvable : %s", path.c_str());
            ok = false;
        }
    }
    if (!ok) {
        // Sans les fichiers de police, on garde au moins celle d'ImGui.
        ImFont* fallback = io.Fonts->AddFontDefault();
        for (ImFont*& f : g_fonts)
            if (!f)
                f = fallback;
    }
    io.FontDefault = g_fonts[static_cast<int>(Weight::Medium)];
    return ok;
}

ImFont* font(Weight weight)
{
    return g_fonts[static_cast<int>(weight)];
}

void prewarmFonts()
{
    // ImGui 1.92 dessine chaque lettre dans sa texture la première fois
    // qu'elle apparaît dans une taille donnée. Pendant cette image-là, une
    // lettre pouvait s'afficher avec un bout de sa voisine. En préparant
    // tout au démarrage, plus aucune lettre n'est ajoutée en cours de route.
    static const char* const kCharacters =
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
        "àâäçéèêëîïôöùûüÿœæÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ«»…’·–—°€×●→←↻✓";
    // Toutes les tailles (en pixels de design) utilisées par les écrans.
    static const float kSizes[] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 24, 26, 30, 34, 38, 40, 56};
    for (ImFont* f : g_fonts) {
        if (!f)
            continue;
        for (float size : kSizes) {
            ImFontBaked* baked = f->GetFontBaked(px(size));
            // Décodage UTF-8 du texte, lettre par lettre.
            for (const char* s = kCharacters; *s;) {
                unsigned int c = 0;
                s += ImTextCharFromUtf8(&c, s, nullptr);
                baked->FindGlyph(static_cast<ImWchar>(c));
            }
        }
    }
    const ImTextureData* tex = ImGui::GetIO().Fonts->TexData;
    SDL_Log("Polices préparées : texture de %d x %d", tex->Width, tex->Height);
}

void setScale(float value)
{
    g_scale = value > 0.0f ? value : 1.0f;
}

float scale()
{
    return g_scale;
}

void applyImGuiStyle()
{
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowRounding = px(24.0f);
    style.ChildRounding = px(24.0f);
    style.PopupRounding = px(14.0f);
    style.FrameRounding = px(999.0f);
    style.ScrollbarRounding = px(999.0f);
    style.ScrollbarSize = px(10.0f);
    style.GrabRounding = px(999.0f);
    style.FramePadding = px(16.0f, 12.0f);
    style.ItemSpacing = px(10.0f, 10.0f);
    style.FontSizeBase = px(14.0f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = toVec4(Ink);
    c[ImGuiCol_TextDisabled] = toVec4(Muted);
    c[ImGuiCol_WindowBg] = toVec4(Ground);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = toVec4(Surface);
    c[ImGuiCol_Border] = toVec4(Hairline);
    c[ImGuiCol_FrameBg] = toVec4(Surface);
    c[ImGuiCol_FrameBgHovered] = toVec4(Surface);
    c[ImGuiCol_FrameBgActive] = toVec4(Surface);
    c[ImGuiCol_Header] = toVec4(NeutralSoft);
    c[ImGuiCol_HeaderHovered] = toVec4(Ground);
    c[ImGuiCol_HeaderActive] = toVec4(NeutralSoft);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = toVec4(Hairline);
    c[ImGuiCol_ScrollbarGrabHovered] = toVec4(Dashed);
    c[ImGuiCol_ScrollbarGrabActive] = toVec4(Muted);
    c[ImGuiCol_NavCursor] = toVec4(Accent);
    c[ImGuiCol_TextSelectedBg] = toVec4(withAlpha(Accent, 60));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.067f, 0.067f, 0.078f, 0.42f);
}

} // namespace theme
