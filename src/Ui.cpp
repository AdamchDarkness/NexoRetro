#include "Ui.hpp"

#include "Lang.hpp"
#include "UiSounds.hpp"

#include "ui/Draw.hpp"
#include "ui/Theme.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include <algorithm>

using theme::px;

namespace {
constexpr Uint64 kNotificationMs = 2600;
}

Ui::~Ui()
{
    shutdownRenderer();
    if (m_initialized) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
}

bool Ui::init(SDL_Window* window, const std::filesystem::path& fontDir)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_initialized = true;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // pas de fichier imgui.ini : on place tout nous-mêmes
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    // Écrans haute résolution : Windows peut demander un agrandissement (125 %, 150 %...).
    m_displayScale = SDL_GetWindowDisplayScale(window);
    if (m_displayScale <= 0.0f)
        m_displayScale = 1.0f;
    theme::setScale(m_displayScale * m_zoom);
    m_fontDir = fontDir;
    theme::loadFonts(fontDir, lang::current());
    theme::applyImGuiStyle();

    return ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext());
}

bool Ui::initRenderer()
{
    if (m_rendererReady)
        return true;
    m_rendererReady = ImGui_ImplOpenGL3_Init("#version 330 core");
    return m_rendererReady;
}

void Ui::shutdownRenderer()
{
    if (m_rendererReady)
        ImGui_ImplOpenGL3_Shutdown();
    m_rendererReady = false;
}

void Ui::processEvent(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void Ui::beginFrame()
{
    lang::newFrame();
    // Nouvelle langue d'une autre écriture (chinois, arabe...) : on recharge
    // les polices, entre deux images. Le moteur de rendu d'ImGui est arrêté
    // le temps de vider la texture des lettres, puis relancé.
    if (!theme::fontsMatch(lang::current())) {
        const bool renderer = m_rendererReady;
        shutdownRenderer();
        ImGui::GetIO().Fonts->Clear();
        theme::loadFonts(m_fontDir, lang::current());
        if (renderer)
            initRenderer();
        m_fontsReady = false;
        SDL_Log("Polices rechargées pour la langue %s", lang::current().c_str());
    }
    // Nouvelle taille d'interface : tout est dessiné en px(), il suffit de
    // changer l'échelle (et les arrondis du style, eux aussi en px()).
    if (m_wantedZoom != m_zoom) {
        m_zoom = m_wantedZoom;
        theme::setScale(m_displayScale * m_zoom);
        theme::applyImGuiStyle();
        m_fontsReady = false;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (!m_fontsReady) {
        theme::prewarmFonts();
        m_fontsReady = true;
    }
}

void Ui::endFrame()
{
    // Les sons de l'interface : clics, survols, navigation (voir UiSounds.hpp).
    sounds::watchImGui();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Ui::notify(std::string text)
{
    SDL_Log("%s", text.c_str());
    sounds::play(sounds::Event::Notify);
    m_notifications.push_back({std::move(text), SDL_GetTicks()});
}

void Ui::drawNotifications()
{
    const Uint64 now = SDL_GetTicks();
    // [C++20] std::erase_if retire du vector tous les éléments qui vérifient la condition.
    std::erase_if(m_notifications, [now](const Notification& n) { return now - n.createdAt > kNotificationMs; });

    // Des pilules blanches empilées en haut à droite, qui s'effacent en fondu.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float y = viewport->Pos.y + px(92.0f);
    for (const Notification& n : m_notifications) {
        const float age = static_cast<float>(now - n.createdAt);
        const float fade = std::clamp((kNotificationMs - age) / 300.0f, 0.0f, 1.0f) * std::clamp(age / 150.0f, 0.0f, 1.0f);
        const int alpha = static_cast<int>(255 * fade);
        const ImVec2 ts = draw::measure(theme::Weight::Bold, 14.0f, n.text);
        const ImVec2 size(ts.x + px(52.0f), px(46.0f));
        const ImVec2 min(viewport->Pos.x + viewport->Size.x - px(40.0f) - size.x, y);
        const ImVec2 max(min.x + size.x, min.y + size.y);
        draw::shadow(dl, min, max, size.y / 2, px(14.0f), px(8.0f), static_cast<int>(40 * fade));
        draw::rect(dl, min, max, theme::withAlpha(theme::Surface, alpha), size.y / 2, theme::withAlpha(theme::Hairline, alpha));
        dl->AddCircleFilled(ImVec2(min.x + px(20.0f), min.y + size.y / 2), px(4.0f), theme::withAlpha(theme::Accent, alpha));
        draw::text(dl, ImVec2(min.x + px(32.0f), min.y + (size.y - ts.y) / 2), theme::Weight::Bold, 14.0f,
                   theme::withAlpha(theme::Ink, alpha), n.text);
        y += size.y + px(8.0f);
    }
}
