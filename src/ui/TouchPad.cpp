#include "TouchPad.hpp"

#include "Draw.hpp"
#include "Theme.hpp"

#include <imgui.h>
#include <libretro.h>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

constexpr uint16_t bit(unsigned id)
{
    return static_cast<uint16_t>(1u << id);
}

bool inRect(float x, float y, float x0, float y0, float w, float h)
{
    return x >= x0 && x <= x0 + w && y >= y0 && y <= y0 + h;
}

// Les quatre boutons de droite (disposition Super Nintendo) : position
// relative au centre, bouton du RetroPad, nom affiché.
struct Face {
    float dx, dy;
    unsigned id;
    const char* label;
};
constexpr Face kFaces[] = {{0, -1, RETRO_DEVICE_ID_JOYPAD_X, "X"},
                           {-1, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y"},
                           {1, 0, RETRO_DEVICE_ID_JOYPAD_A, "A"},
                           {0, 1, RETRO_DEVICE_ID_JOYPAD_B, "B"}};

} // namespace

void TouchPad::handleEvent(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION: {
        // (SDL donne la position entre 0 et 1 : on la ramène à la taille de l'écran.)
        const SDL_FPoint p{event.tfinger.x * m_layout.width, event.tfinger.y * m_layout.height};
        m_fingers[event.tfinger.fingerID] = p;
        // Un appui très bref (doigt posé et levé entre deux images) compte
        // quand même : le bouton reste appuyé pendant au moins une image.
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            m_tapped |= buttonsAt(p.x, p.y);
            if (std::hypot(p.x - m_layout.width / 2, p.y - (m_layout.margin + m_layout.unit * 0.05f)) < m_layout.unit * 0.07f)
                m_menu = true;
        }
        break;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        m_fingers.erase(event.tfinger.fingerID);
        break;
    default:
        break;
    }
}

void TouchPad::clear()
{
    m_fingers.clear();
    m_buttons = 0;
    m_tapped = 0;
    m_menuDown = false;
}

bool TouchPad::takeMenu()
{
    const bool v = m_menu;
    m_menu = false;
    return v;
}

void TouchPad::update(float width, float height)
{
    // La disposition suit la taille de l'écran (téléphone ou tablette, en largeur).
    Layout& l = m_layout;
    l.width = width;
    l.height = height;
    l.unit = std::min(width, height);
    l.margin = l.unit * 0.04f;
    l.dpadR = l.unit * 0.15f;
    l.dpadX = l.margin + l.dpadR * 1.25f;
    l.dpadY = height - l.margin - l.dpadR * 1.35f;
    l.faceR = l.unit * 0.065f;
    l.faceGap = l.unit * 0.115f;
    l.faceX = width - l.margin - l.faceGap - l.faceR * 1.4f;
    l.faceY = height - l.margin - l.faceGap - l.faceR * 1.6f;
    l.shoulderW = l.unit * 0.22f;
    l.shoulderH = l.unit * 0.085f;

    uint16_t buttons = 0;
    bool menu = false;
    for (const auto& [id, p] : m_fingers) {
        buttons |= buttonsAt(p.x, p.y);
        // Le bouton de menu : en haut au centre.
        if (std::hypot(p.x - width / 2, p.y - (l.margin + l.unit * 0.05f)) < l.unit * 0.07f)
            menu = true;
    }
    // (Le menu s'ouvre au moment où le doigt se pose : voir handleEvent.)
    m_menuDown = menu;
    m_buttons = buttons | m_tapped;
    m_tapped = 0;
}

uint16_t TouchPad::buttonsAt(float x, float y) const
{
    const Layout& l = m_layout;
    uint16_t b = 0;
    // Croix directionnelle : selon l'angle du doigt autour du centre (8
    // directions), dans un cercle un peu plus grand que celui dessiné.
    const float dx = x - l.dpadX;
    const float dy = y - l.dpadY;
    const float dist = std::hypot(dx, dy);
    if (dist < l.dpadR * 1.6f && dist > l.dpadR * 0.18f) {
        const float threshold = dist * 0.38f; // ~22° : les diagonales sont faciles à prendre
        if (dx < -threshold) b |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
        if (dx > threshold) b |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);
        if (dy < -threshold) b |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
        if (dy > threshold) b |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
        return b;
    }
    // Boutons A B X Y : le plus proche du doigt (on glisse de l'un à l'autre).
    float best = l.faceR * 1.7f;
    unsigned bestId = 99;
    for (const Face& f : kFaces) {
        const float d = std::hypot(x - (l.faceX + f.dx * l.faceGap), y - (l.faceY + f.dy * l.faceGap));
        if (d < best) {
            best = d;
            bestId = f.id;
        }
    }
    if (bestId != 99)
        return bit(bestId);
    // Gâchettes : L / R en haut, L2 / R2 juste en dessous.
    const float top = l.margin;
    if (inRect(x, y, l.margin, top, l.shoulderW, l.shoulderH))
        return bit(RETRO_DEVICE_ID_JOYPAD_L);
    if (inRect(x, y, l.width - l.margin - l.shoulderW, top, l.shoulderW, l.shoulderH))
        return bit(RETRO_DEVICE_ID_JOYPAD_R);
    const float top2 = top + l.shoulderH + l.margin * 0.5f;
    if (inRect(x, y, l.margin, top2, l.shoulderW * 0.7f, l.shoulderH * 0.85f))
        return bit(RETRO_DEVICE_ID_JOYPAD_L2);
    if (inRect(x, y, l.width - l.margin - l.shoulderW * 0.7f, top2, l.shoulderW * 0.7f, l.shoulderH * 0.85f))
        return bit(RETRO_DEVICE_ID_JOYPAD_R2);
    // Select et Start : en bas, au centre.
    const float pillW = l.unit * 0.16f;
    const float pillH = l.unit * 0.07f;
    const float pillY = l.height - l.margin - pillH;
    if (inRect(x, y, l.width / 2 - pillW - l.margin * 0.5f, pillY - pillH * 0.4f, pillW, pillH * 1.8f))
        return bit(RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (inRect(x, y, l.width / 2 + l.margin * 0.5f, pillY - pillH * 0.4f, pillW, pillH * 1.8f))
        return bit(RETRO_DEVICE_ID_JOYPAD_START);
    return 0;
}

void TouchPad::draw(float opacity) const
{
    const Layout& l = m_layout;
    if (l.unit <= 0)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int a = static_cast<int>(255 * std::clamp(opacity, 0.1f, 1.0f));
    const ImU32 fill = IM_COL32(255, 255, 255, a * 45 / 255);
    const ImU32 line = IM_COL32(255, 255, 255, a * 120 / 255);
    const ImU32 pressed = IM_COL32(110, 140, 255, a * 200 / 255);
    const ImU32 text = IM_COL32(255, 255, 255, a * 220 / 255);
    auto down = [&](unsigned id) { return (m_buttons & bit(id)) != 0; };
    auto label = [&](ImVec2 center, const char* s, float size) {
        const ImVec2 ts = draw::measure(theme::Weight::ExtraBold, size, s);
        draw::text(dl, ImVec2(center.x - ts.x / 2, center.y - ts.y / 2), theme::Weight::ExtraBold, size, text, s);
    };
    // (Tailles des textes : en "pixels de design", convertis par draw::.)
    const float fontSize = l.unit * 0.045f / theme::scale();

    // Croix : un disque, et les quatre branches.
    dl->AddCircleFilled(ImVec2(l.dpadX, l.dpadY), l.dpadR * 1.15f, fill, 48);
    dl->AddCircle(ImVec2(l.dpadX, l.dpadY), l.dpadR * 1.15f, line, 48, 2.0f);
    const float arm = l.dpadR * 0.42f;
    struct Arm {
        float dx, dy;
        unsigned id;
    };
    const Arm arms[] = {{0, -1, RETRO_DEVICE_ID_JOYPAD_UP}, {0, 1, RETRO_DEVICE_ID_JOYPAD_DOWN},
                        {-1, 0, RETRO_DEVICE_ID_JOYPAD_LEFT}, {1, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT}};
    for (const Arm& r : arms) {
        const ImVec2 c(l.dpadX + r.dx * l.dpadR * 0.62f, l.dpadY + r.dy * l.dpadR * 0.62f);
        dl->AddRectFilled(ImVec2(c.x - arm, c.y - arm), ImVec2(c.x + arm, c.y + arm), down(r.id) ? pressed : fill, arm * 0.4f);
    }
    // Boutons A B X Y.
    for (const Face& f : kFaces) {
        const ImVec2 c(l.faceX + f.dx * l.faceGap, l.faceY + f.dy * l.faceGap);
        dl->AddCircleFilled(c, l.faceR, down(f.id) ? pressed : fill, 32);
        dl->AddCircle(c, l.faceR, line, 32, 2.0f);
        label(c, f.label, fontSize);
    }
    // Gâchettes.
    auto shoulder = [&](float x, float y, float w, float h, unsigned id, const char* s) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), down(id) ? pressed : fill, h * 0.45f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), line, h * 0.45f, 0, 2.0f);
        label(ImVec2(x + w / 2, y + h / 2), s, fontSize * 0.85f);
    };
    const float top2 = l.margin + l.shoulderH + l.margin * 0.5f;
    shoulder(l.margin, l.margin, l.shoulderW, l.shoulderH, RETRO_DEVICE_ID_JOYPAD_L, "L");
    shoulder(l.width - l.margin - l.shoulderW, l.margin, l.shoulderW, l.shoulderH, RETRO_DEVICE_ID_JOYPAD_R, "R");
    shoulder(l.margin, top2, l.shoulderW * 0.7f, l.shoulderH * 0.85f, RETRO_DEVICE_ID_JOYPAD_L2, "L2");
    shoulder(l.width - l.margin - l.shoulderW * 0.7f, top2, l.shoulderW * 0.7f, l.shoulderH * 0.85f, RETRO_DEVICE_ID_JOYPAD_R2,
             "R2");
    // Select et Start.
    const float pillW = l.unit * 0.16f;
    const float pillH = l.unit * 0.07f;
    const float pillY = l.height - l.margin - pillH;
    shoulder(l.width / 2 - pillW - l.margin * 0.5f, pillY, pillW, pillH, RETRO_DEVICE_ID_JOYPAD_SELECT, "SELECT");
    shoulder(l.width / 2 + l.margin * 0.5f, pillY, pillW, pillH, RETRO_DEVICE_ID_JOYPAD_START, "START");
    // Menu : trois traits dans un rond, en haut au centre.
    const ImVec2 m(l.width / 2, l.margin + l.unit * 0.05f);
    const float r = l.unit * 0.045f;
    dl->AddCircleFilled(m, r, m_menuDown ? pressed : fill, 32);
    dl->AddCircle(m, r, line, 32, 2.0f);
    for (int i = -1; i <= 1; ++i)
        dl->AddLine(ImVec2(m.x - r * 0.45f, m.y + i * r * 0.32f), ImVec2(m.x + r * 0.45f, m.y + i * r * 0.32f), text, 2.5f);
}

} // namespace ui
