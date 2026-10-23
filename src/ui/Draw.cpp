#include "Draw.hpp"

#include "../Lang.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using theme::px;

namespace draw {

void shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, float spread, float offsetY, int alpha)
{
    // Chaque couche est très transparente : là où elles se superposent toutes
    // (près de la carte), l'ombre atteint "alpha" ; vers l'extérieur, de
    // moins en moins de couches se recouvrent, d'où le dégradé.
    constexpr int kLayers = 10;
    const float perLayer = static_cast<float>(alpha) / kLayers;
    for (int i = kLayers; i >= 1; --i) {
        const float t = static_cast<float>(i) / kLayers; // 1 = couche la plus large
        const float grow = spread * t - spread * 0.35f;  // les premières couches restent sous la carte
        const int a = std::max(1, static_cast<int>(perLayer + 0.5f));
        dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow + offsetY), ImVec2(max.x + grow, max.y + grow + offsetY),
                          IM_COL32(17, 17, 20, a), std::max(0.0f, rounding + grow));
    }
}

void rect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 fill, float rounding, ImU32 border)
{
    if (fill)
        dl->AddRectFilled(min, max, fill, rounding);
    if (border)
        dl->AddRect(min, max, border, rounding, 0, px(1.0f));
}

void dashedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float rounding, float thickness)
{
    // On parcourt le contour arrondi point par point, et on ne trace qu'un
    // segment sur deux.
    dl->PathRect(ImVec2(min.x + thickness / 2, min.y + thickness / 2), ImVec2(max.x - thickness / 2, max.y - thickness / 2),
                 rounding);
    ImVector<ImVec2> points = dl->_Path;
    dl->PathClear();
    if (points.Size < 2)
        return;
    const float dash = px(7.0f);
    const float gap = px(5.0f);
    float carried = 0.0f;
    bool drawing = true;
    for (int i = 0; i < points.Size; ++i) {
        ImVec2 a = points[i];
        const ImVec2 b = points[(i + 1) % points.Size];
        float segment = std::hypot(b.x - a.x, b.y - a.y);
        while (segment > 0.0f) {
            const float step = std::min(segment, (drawing ? dash : gap) - carried);
            const float t = step / segment;
            const ImVec2 c(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            if (drawing)
                dl->AddLine(a, c, color, thickness);
            carried += step;
            segment -= step;
            a = c;
            if (carried >= (drawing ? dash : gap) - 0.01f) {
                drawing = !drawing;
                carried = 0.0f;
            }
        }
    }
}

// Tous les textes passent par ici : c'est là qu'ils sont traduits (Lang.hpp).
ImVec2 measure(theme::Weight weight, float size, std::string_view text)
{
    text = lang::tr(text);
    ImFont* f = theme::font(weight);
    return f->CalcTextSizeA(px(size), FLT_MAX, 0.0f, text.data(), text.data() + text.size());
}

void text(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view text)
{
    text = lang::tr(text);
    dl->AddText(theme::font(weight), px(size), pos, color, text.data(), text.data() + text.size());
}

void textEllipsis(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view value,
                  float maxWidth)
{
    // Traduit d'abord, puis raccourci : les morceaux coupés ne doivent pas
    // être retraduits (d'où le cache : tr() de la traduction = elle-même).
    value = lang::tr(value);
    if (measure(weight, size, value).x <= maxWidth) {
        text(dl, pos, weight, size, color, value);
        return;
    }
    // On retire des caractères jusqu'à ce que "texte…" tienne. On recule
    // octet par octet en évitant de couper un caractère UTF-8 en deux.
    std::string cut(value);
    while (!cut.empty()) {
        do {
            cut.pop_back();
        } while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80);
        if (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0x80))
            cut.pop_back(); // premier octet d'un caractère multi-octets
        const std::string candidate = cut + "…";
        if (measure(weight, size, candidate).x <= maxWidth) {
            text(dl, pos, weight, size, color, candidate);
            return;
        }
    }
}

float textWrapped(ImDrawList* dl, ImVec2 pos, theme::Weight weight, float size, ImU32 color, std::string_view value,
                  float wrapWidth)
{
    value = lang::tr(value);
    dl->AddText(theme::font(weight), px(size), pos, color, value.data(), value.data() + value.size(), wrapWidth);
    return wrappedHeight(weight, size, value, wrapWidth);
}

float wrappedHeight(theme::Weight weight, float size, std::string_view value, float wrapWidth)
{
    value = lang::tr(value);
    return theme::font(weight)->CalcTextSizeA(px(size), FLT_MAX, wrapWidth, value.data(), value.data() + value.size()).y;
}

// --- Icônes ------------------------------------------------------------

namespace {

// Convertit un point de la grille 24 x 24 en position à l'écran.
struct Grid {
    ImVec2 origin;
    float unit;
    ImVec2 operator()(float x, float y) const { return ImVec2(origin.x + x * unit, origin.y + y * unit); }
};

void polyline(ImDrawList* dl, const Grid& g, std::initializer_list<ImVec2> points, ImU32 color, float thickness,
              bool closed = false)
{
    for (const ImVec2& p : points)
        dl->PathLineTo(g(p.x, p.y));
    dl->PathStroke(color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
}

void arc(ImDrawList* dl, const Grid& g, float cx, float cy, float r, float fromDeg, float toDeg, ImU32 color,
         float thickness)
{
    constexpr float kDeg = 3.14159265f / 180.0f;
    dl->PathArcTo(g(cx, cy), r * g.unit, fromDeg * kDeg, toDeg * kDeg, 24);
    dl->PathStroke(color, ImDrawFlags_None, thickness);
}

} // namespace

void icon(ImDrawList* dl, Icon which, ImVec2 topLeft, float size, ImU32 color)
{
    const Grid g{topLeft, size / 24.0f};
    const float t = std::max(1.0f, size / 24.0f * 2.0f); // épaisseur du trait
    switch (which) {
    case Icon::None:
        break;
    case Icon::Play:
        dl->AddTriangleFilled(g(8, 5.5f), g(8, 18.5f), g(19, 12), color);
        break;
    case Icon::Pause:
        dl->AddRectFilled(g(7, 5), g(10.5f, 19), color, g.unit);
        dl->AddRectFilled(g(13.5f, 5), g(17, 19), color, g.unit);
        break;
    case Icon::Plus:
        dl->AddLine(g(12, 5), g(12, 19), color, t * 1.15f);
        dl->AddLine(g(5, 12), g(19, 12), color, t * 1.15f);
        break;
    case Icon::Search:
        dl->AddCircle(g(11, 11), 7 * g.unit, color, 24, t);
        dl->AddLine(g(16.2f, 16.2f), g(20, 20), color, t);
        break;
    case Icon::Close:
        dl->AddLine(g(6, 6), g(18, 18), color, t * 1.1f);
        dl->AddLine(g(18, 6), g(6, 18), color, t * 1.1f);
        break;
    case Icon::Check:
        polyline(dl, g, {{5, 12.5f}, {9.5f, 17}, {19, 7.5f}}, color, t * 1.3f);
        break;
    case Icon::ChevronLeft:
        polyline(dl, g, {{15, 6}, {9, 12}, {15, 18}}, color, t * 1.1f);
        break;
    case Icon::ChevronRight:
        polyline(dl, g, {{9, 6}, {15, 12}, {9, 18}}, color, t * 1.1f);
        break;
    case Icon::Folder:
        polyline(dl, g, {{3, 9}, {3, 6}, {4, 5}, {9, 5}, {11, 7}, {20, 7}, {21, 8}, {21, 18}, {20, 19}, {4, 19}, {3, 18}},
                 color, t, true);
        break;
    case Icon::Upload:
        dl->AddLine(g(12, 16), g(12, 4), color, t);
        polyline(dl, g, {{7, 9}, {12, 4}, {17, 9}}, color, t);
        polyline(dl, g, {{4, 16}, {4, 19}, {5, 20}, {19, 20}, {20, 19}, {20, 16}}, color, t);
        break;
    case Icon::Chip:
        dl->AddRect(g(6, 6), g(18, 18), color, 2 * g.unit, 0, t);
        for (float k : {9.0f, 15.0f}) {
            dl->AddLine(g(k, 2), g(k, 6), color, t);
            dl->AddLine(g(k, 18), g(k, 22), color, t);
            dl->AddLine(g(2, k), g(6, k), color, t);
            dl->AddLine(g(18, k), g(22, k), color, t);
        }
        break;
    case Icon::Image:
        dl->AddRect(g(3, 4), g(21, 20), color, 2 * g.unit, 0, t);
        dl->AddCircle(g(9, 10), 2 * g.unit, color, 12, t);
        polyline(dl, g, {{21, 16}, {16, 11}, {7, 20}}, color, t);
        break;
    case Icon::Refresh:
        arc(dl, g, 12, 12, 8, 200, 330, color, t);
        polyline(dl, g, {{19.6f, 4.8f}, {19.6f, 9.2f}, {15.3f, 9.2f}}, color, t);
        arc(dl, g, 12, 12, 8, 20, 150, color, t);
        polyline(dl, g, {{4.4f, 19.2f}, {4.4f, 14.8f}, {8.7f, 14.8f}}, color, t);
        break;
    case Icon::Users:
        dl->AddCircle(g(9, 8), 3.5f * g.unit, color, 16, t);
        arc(dl, g, 9, 21, 6.5f, 200, 340, color, t);
        arc(dl, g, 15.5f, 8, 3.5f, -80, 80, color, t);
        arc(dl, g, 16, 21, 5.5f, 260, 340, color, t);
        break;
    case Icon::Heart:
    case Icon::HeartOutline: {
        // Deux demi-cercles et une pointe.
        dl->PathArcTo(g(8.3f, 9.8f), 4.2f * g.unit, 3.14159f * 0.80f, 3.14159f * 2.0f, 16);
        dl->PathArcTo(g(15.7f, 9.8f), 4.2f * g.unit, 3.14159f, 3.14159f * 2.20f, 16);
        dl->PathLineTo(g(12, 20.5f));
        if (which == Icon::Heart)
            dl->PathFillConcave(color);
        else
            dl->PathStroke(color, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::Gamepad:
        dl->AddRect(g(2.5f, 8), g(21.5f, 18), color, 5 * g.unit, 0, t);
        dl->AddLine(g(8, 11), g(8, 15), color, t);
        dl->AddLine(g(6, 13), g(10, 13), color, t);
        dl->AddCircleFilled(g(15.5f, 12), 1.1f * g.unit, color);
        dl->AddCircleFilled(g(17.8f, 14.2f), 1.1f * g.unit, color);
        break;
    case Icon::Pencil:
        polyline(dl, g, {{4, 20}, {4.5f, 16}, {15.5f, 5}, {19, 8.5f}, {8, 19.5f}}, color, t, true);
        dl->AddLine(g(13, 7.5f), g(16.5f, 11), color, t);
        break;
    case Icon::Trash:
        dl->AddLine(g(4, 6.5f), g(20, 6.5f), color, t);                                   // couvercle
        polyline(dl, g, {{9.5f, 6.5f}, {9.5f, 4}, {14.5f, 4}, {14.5f, 6.5f}}, color, t);    // poignée
        polyline(dl, g, {{6, 6.5f}, {7, 20}, {17, 20}, {18, 6.5f}}, color, t);              // poubelle
        dl->AddLine(g(10, 10), g(10, 16.5f), color, t);
        dl->AddLine(g(14, 10), g(14, 16.5f), color, t);
        break;
    case Icon::Copy:
        dl->AddRect(g(8, 8), g(20, 20), color, 2 * g.unit, 0, t);
        polyline(dl, g, {{16, 6}, {16, 5}, {15, 4}, {5, 4}, {4, 5}, {4, 15}, {5, 16}, {6, 16}}, color, t);
        break;
    case Icon::Mic:
    case Icon::MicOff:
        dl->AddRect(g(9, 3), g(15, 14), color, 3 * g.unit, 0, t);
        arc(dl, g, 12, 10.5f, 6, 0, 180, color, t);
        dl->AddLine(g(12, 16.5f), g(12, 20.5f), color, t);
        dl->AddLine(g(8.5f, 20.5f), g(15.5f, 20.5f), color, t);
        if (which == Icon::MicOff)
            dl->AddLine(g(4, 4), g(20, 20), color, t * 1.1f);
        break;
    case Icon::Speaker:
    case Icon::SpeakerOff:
        polyline(dl, g, {{3, 9.5f}, {7, 9.5f}, {12, 5}, {12, 19}, {7, 14.5f}, {3, 14.5f}}, color, t, true);
        if (which == Icon::Speaker) {
            arc(dl, g, 12, 12, 4, -50, 50, color, t);
            arc(dl, g, 12, 12, 8, -50, 50, color, t);
        } else {
            dl->AddLine(g(15.5f, 9), g(21, 15), color, t);
            dl->AddLine(g(21, 9), g(15.5f, 15), color, t);
        }
        break;
    case Icon::Grid:
        for (float y : {4.0f, 13.5f})
            for (float x : {4.0f, 13.5f})
                dl->AddRect(g(x, y), g(x + 6.5f, y + 6.5f), color, 1.5f * g.unit, 0, t);
        break;
    case Icon::List:
        for (float y : {6.0f, 12.0f, 18.0f}) {
            dl->AddCircleFilled(g(4.5f, y), 1.3f * g.unit, color);
            dl->AddLine(g(8.5f, y), g(20.5f, y), color, t);
        }
        break;
    case Icon::Carousel:
        dl->AddRect(g(7.5f, 4), g(16.5f, 20), color, 2 * g.unit, 0, t);
        polyline(dl, g, {{4, 7}, {2.5f, 7}, {2.5f, 17}, {4, 17}}, color, t);
        polyline(dl, g, {{20, 7}, {21.5f, 7}, {21.5f, 17}, {20, 17}}, color, t);
        break;
    }
}

} // namespace draw
