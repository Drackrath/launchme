// Drawn controller pictures: stylised silhouettes built from Direct2D path geometry,
// no bundled or downloaded artwork. Coordinates are in the controller_art design
// canvas (440 x 300, centre line x = 220); see controller_art.hpp for the layers.
#include "renderer.hpp"

#include <d2d1helper.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
using namespace controller_art;
constexpr float cx = width * 0.5f;
constexpr float pi = std::numbers::pi_v<float>;

// One cubic segment of a right-half outline; x is relative to the centre line.
struct Seg { float x1, y1, x2, y2, x, y; };
constexpr Seg L(float x, float y) { return {x, y, x, y, x, y}; } // Straight segment.

class Builder {
public:
    explicit Builder(ID2D1Factory* factory) : factory_(factory) {
        for (int i = 0; i < layer_count && ok_; ++i) {
            ok_ = SUCCEEDED(factory->CreatePathGeometry(geometry_[i].GetAddressOf())) &&
                  SUCCEEDED(geometry_[i]->Open(sinks_[i].GetAddressOf()));
            if (ok_) sinks_[i]->SetFillMode(D2D1_FILL_MODE_WINDING);
        }
    }
    // Symmetric outline: `right` runs clockwise from (cx, top) down the right side
    // and ends on the centre line; the left half is its mirror image.
    void mirrored(Layer layer, float top, std::initializer_list<Seg> right) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        const std::vector<Seg> segments(right);
        sink->BeginFigure({cx, top}, D2D1_FIGURE_BEGIN_FILLED);
        for (const auto& s : segments) sink->AddBezier(D2D1::BezierSegment({cx + s.x1, s.y1}, {cx + s.x2, s.y2}, {cx + s.x, s.y}));
        for (size_t i = segments.size(); i-- > 0;) {
            const auto& s = segments[i];
            const float x = i ? segments[i - 1].x : 0, y = i ? segments[i - 1].y : top;
            sink->AddBezier(D2D1::BezierSegment({cx - s.x2, s.y2}, {cx - s.x1, s.y1}, {cx - x, y}));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    void circle(Layer layer, float x, float y, float r) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        sink->BeginFigure({x + r, y}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment({x - r, y}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddArc(D2D1::ArcSegment({x + r, y}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    // Open arc, degrees clockwise from +x.
    void arc(Layer layer, float x, float y, float r, float from, float to) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        const auto at = [&](float degrees) {
            const float a = degrees * pi / 180;
            return D2D1::Point2F(x + r * std::cos(a), y + r * std::sin(a));
        };
        sink->BeginFigure(at(from), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(at(to), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      to - from > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    void rect(Layer layer, float l, float t, float r, float b, float tl, float tr, float br, float bl) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        const auto corner = [&](D2D1_POINT_2F to, float radius) {
            if (radius > 0) sink->AddArc(D2D1::ArcSegment(to, {radius, radius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            else sink->AddLine(to);
        };
        sink->BeginFigure({l + tl, t}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine({r - tr, t}); corner({r, t + tr}, tr);
        sink->AddLine({r, b - br}); corner({r - br, b}, br);
        sink->AddLine({l + bl, b}); corner({l, b - bl}, bl);
        sink->AddLine({l, t + tl}); corner({l + tl, t}, tl);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    void rect(Layer layer, float l, float t, float r, float b, float radius) { rect(layer, l, t, r, b, radius, radius, radius, radius); }
    // Stadium of `length` x `thickness`, turned by `degrees` (clockwise on screen).
    void pill(Layer layer, float x, float y, float length, float thickness, float degrees = 0) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        const float a = degrees * pi / 180, ux = std::cos(a), uy = std::sin(a), nx = -uy, ny = ux;
        const float r = thickness * 0.5f, half = std::max(0.0f, length * 0.5f - r);
        const auto p = [&](float along, float side) { return D2D1::Point2F(x + ux * along + nx * side, y + uy * along + ny * side); };
        sink->BeginFigure(p(-half, r), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(p(half, r));
        sink->AddArc(D2D1::ArcSegment(p(half, -r), {r, r}, 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddLine(p(-half, -r));
        sink->AddArc(D2D1::ArcSegment(p(-half, r), {r, r}, 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    void polygon(Layer layer, std::initializer_list<D2D1_POINT_2F> points, bool closed = true) {
        auto* sink = sinks_[layer].Get();
        if (!sink || points.size() < 2) return;
        auto it = points.begin();
        sink->BeginFigure(*it, closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        for (++it; it != points.end(); ++it) sink->AddLine(*it);
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }
    void line(Layer layer, float x1, float y1, float x2, float y2) { polygon(layer, {{x1, y1}, {x2, y2}}, false); }
    void cross(Layer layer, float x, float y, float arm, float half) {
        polygon(layer, {{x - half, y - arm}, {x + half, y - arm}, {x + half, y - half}, {x + arm, y - half},
                        {x + arm, y + half}, {x + half, y + half}, {x + half, y + arm}, {x - half, y + arm},
                        {x - half, y + half}, {x - arm, y + half}, {x - arm, y - half}, {x - half, y - half}});
    }
    void star(Layer layer, float x, float y, float outer, float inner) {
        auto* sink = sinks_[layer].Get();
        if (!sink) return;
        for (int i = 0; i < 10; ++i) {
            const float a = -pi / 2 + i * pi / 5, r = i % 2 ? inner : outer;
            const D2D1_POINT_2F point{x + r * std::cos(a), y + r * std::sin(a)};
            if (!i) sink->BeginFigure(point, D2D1_FIGURE_BEGIN_FILLED); else sink->AddLine(point);
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    // Four separate direction keys pointing at the centre (PlayStation style).
    void arrows(Layer layer, float x, float y, float inner, float outer, float half) {
        for (int k = 0; k < 4; ++k) {
            const float a = k * pi / 2, ux = std::cos(a), uy = std::sin(a), nx = -uy, ny = ux;
            const auto p = [&](float along, float side) { return D2D1::Point2F(x + ux * along + nx * side, y + uy * along + ny * side); };
            polygon(layer, {p(outer, -half), p(outer, half), p(inner + half, half), p(inner, 0), p(inner + half, -half)});
        }
    }
    // Shared compound parts.
    void stick(float x, float y, float well_radius, float cap_radius) {
        if (well_radius > 0) circle(well, x, y, well_radius);
        circle(key_dark, x, y, cap_radius);
        circle(cap, x, y, cap_radius * 0.66f);
    }
    void diamond(Layer layer, float x, float y, float spacing, float r) {
        circle(layer, x, y - spacing, r); circle(layer, x + spacing, y, r);
        circle(layer, x, y + spacing, r); circle(layer, x - spacing, y, r);
    }
    // Triangle, circle, cross and square engraved on a light diamond.
    void playstation(float x, float y, float spacing, float t) {
        polygon(mark, {{x, y - spacing - t}, {x + t * 0.95f, y - spacing + t * 0.62f}, {x - t * 0.95f, y - spacing + t * 0.62f}});
        circle(mark, x + spacing, y, t * 0.82f);
        line(mark, x - t * 0.7f, y + spacing - t * 0.7f, x + t * 0.7f, y + spacing + t * 0.7f);
        line(mark, x + t * 0.7f, y + spacing - t * 0.7f, x - t * 0.7f, y + spacing + t * 0.7f);
        rect(mark, x - spacing - t * 0.72f, y - t * 0.72f, x - spacing + t * 0.72f, y + t * 0.72f, 0.6f);
    }
    // Round guide button with a crossed sphere, the generic reading of the Xbox mark.
    void guide(float x, float y, float r) {
        circle(key_light, x, y, r);
        circle(mark, x, y, r * 0.62f);
        const float d = r * 0.4f;
        line(mark, x - d, y - d, x + d, y + d); line(mark, x + d, y - d, x - d, y + d);
    }
    bool finish(std::array<ComPtr<ID2D1PathGeometry>, layer_count>& out) {
        for (auto& sink : sinks_) if (sink) { ok_ = SUCCEEDED(sink->Close()) && ok_; sink.Reset(); }
        if (!ok_) return false;
        // Face plates follow the shell edge exactly.
        ComPtr<ID2D1PathGeometry> clipped;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(clipped.GetAddressOf())) || FAILED(clipped->Open(sink.GetAddressOf())) ||
            FAILED(geometry_[inlay]->CombineWithGeometry(geometry_[body].Get(), D2D1_COMBINE_MODE_INTERSECT, nullptr, sink.Get())) ||
            FAILED(sink->Close())) return false;
        geometry_[inlay] = clipped;
        out = geometry_;
        return true;
    }

private:
    ID2D1Factory* factory_;
    bool ok_ = true;
    std::array<ComPtr<ID2D1PathGeometry>, layer_count> geometry_;
    std::array<ComPtr<ID2D1GeometrySink>, layer_count> sinks_;
};

// --- Families ---------------------------------------------------------------

void xbox(Builder& a, bool elite) {
    if (elite) for (float s : {-1.0f, 1.0f}) { // Back paddles peeking below the grips.
        a.pill(under, cx + s * 82, 230, 46, 13, s * 40);
        a.pill(under, cx + s * 106, 250, 46, 13, s * 40);
    }
    a.mirrored(body, 14, {{40, 14, 72, 5, 104, 5}, {148, 5, 182, 20, 197, 50}, {209, 78, 217, 176, 217, 242},
                          {217, 282, 197, 296, 176, 296}, {156, 296, 142, 283, 128, 262}, {110, 234, 96, 212, 60, 210},
                          {36, 208, 16, 208, 0, 208}});
    a.stick(cx - 110, 86, 34, 26);
    a.stick(cx + 56, 152, 32, 24);
    a.circle(key_dark, cx - 56, 154, 32); // D-pad dish.
    a.cross(cap, cx - 56, 154, 23, 8.5f);
    a.diamond(key_dark, cx + 109, 86, 28, 12.5f);
    a.guide(cx, 44, 16);
    a.circle(key_light, cx - 32, 86, 8); a.rect(mark, cx - 35, 83, cx - 29, 89, 1);   // View
    a.circle(key_light, cx + 32, 86, 8);                                                // Menu
    for (float dy : {-2.5f, 0.0f, 2.5f}) a.line(mark, cx + 29, 86 + dy, cx + 35, 86 + dy);
    if (elite) {
        a.circle(mark, cx - 110, 86, 30.5f); a.circle(mark, cx + 56, 152, 28.5f); // Metal stick rings.
        a.circle(mark_light, cx - 56, 154, 24);                                    // Faceted dish.
        for (int i = -1; i <= 1; ++i) a.circle(dot, cx + i * 9.0f, 106, 2.2f);    // Profile LEDs.
        a.pill(key_light, cx, 120, 14, 7);                                         // Profile button.
    } else {
        a.pill(key_light, cx, 110, 16, 8); // Share.
    }
}

void xbox360(Builder& a) {
    a.mirrored(body, 26, {{44, 26, 84, 8, 120, 9}, {164, 10, 194, 36, 205, 76}, {214, 116, 220, 198, 214, 244},
                          {208, 286, 182, 297, 162, 288}, {145, 281, 136, 264, 124, 246}, {108, 220, 90, 204, 58, 202},
                          {34, 200, 14, 202, 0, 202}});
    a.circle(well, cx, 78, 29); // Ring of light in four quadrants around the guide button.
    for (float quadrant : {45.0f, 135.0f, 225.0f, 315.0f}) a.arc(mark, cx, 78, 24.5f, quadrant - 33, quadrant + 33);
    a.guide(cx, 78, 19);
    a.circle(key_light, cx - 46, 98, 7); a.polygon(mark, {{cx - 43.5f, 95}, {cx - 43.5f, 101}, {cx - 49, 98}}); // Back
    a.circle(key_light, cx + 46, 98, 7); a.polygon(mark, {{cx + 43.5f, 95}, {cx + 43.5f, 101}, {cx + 49, 98}}); // Start
    a.stick(cx - 108, 92, 32, 24);
    a.diamond(key_dark, cx + 108, 92, 27, 12.5f);
    a.circle(well, cx - 56, 164, 31); a.circle(key_light, cx - 56, 164, 26); a.cross(mark, cx - 56, 164, 19, 6.5f); // Disc d-pad.
    a.stick(cx + 56, 164, 30, 23);
}

void adaptive(Builder& a) {
    a.rect(body, cx - 212, 62, cx + 212, 238, 38);
    a.rect(plate, cx - 186, 70, cx + 186, 92, 10); // Jack edge.
    for (int i = 0; i < 19; ++i) a.circle(dot, cx - 162 + i * 18.0f, 81, 3.4f);
    for (float x : {cx + 72, cx + 158}) { a.circle(well, x, 166, 44); a.circle(key_dark, x, 166, 38); a.circle(cap, x, 166, 27); }
    a.circle(well, cx - 140, 166, 50); a.cross(key_dark, cx - 140, 166, 36, 13);
    a.guide(cx - 52, 126, 13);
    a.circle(key_light, cx - 52, 166, 7); a.circle(key_light, cx - 52, 200, 7); // View / Menu
}

void dualsense(Builder& a, bool edge) {
    if (edge) for (float s : {-1.0f, 1.0f}) a.pill(under, cx + s * 126, 238, 42, 13, s * 52); // Back buttons.
    a.mirrored(body, 12, {{60, 12, 110, 7, 142, 9}, {178, 11, 200, 32, 208, 70}, {218, 128, 224, 226, 218, 268},
                          {213, 293, 192, 299, 178, 287}, {160, 271, 140, 230, 110, 212}, {80, 196, 40, 194, 0, 194}});
    // Dark centre: from under the touchpad, inside the face buttons, down the inner grips.
    a.mirrored(inlay, 100, {{40, 100, 76, 102, 92, 110}, {108, 120, 118, 150, 126, 196}, {134, 232, 150, 270, 162, 300}, L(0, 300)});
    a.mirrored(plate, 14, {L(86, 14), {89, 44, 86, 76, 79, 94}, {56, 102, 30, 106, 0, 106}});         // Touchpad.
    for (float s : {-1.0f, 1.0f}) a.line(mark, cx + s * 91, 20, cx + s * 85, 92);                     // Light strips.
    a.arrows(key_light, cx - 136, 90, 7, 30, 9);
    a.diamond(key_light, cx + 138, 90, 31, 13.5f);
    a.playstation(cx + 138, 90, 31, 6);
    a.pill(key_light, cx - 100, 40, 16, 7, 100); a.pill(key_light, cx + 100, 40, 16, 7, 80); // Create / Options
    for (float s : {-1.0f, 1.0f}) {
        a.circle(key_dark, cx + s * 70, 152, 26);
        a.circle(mark_light, cx + s * 70, 152, 25);
        a.circle(cap, cx + s * 70, 152, 17);
        if (edge) { a.circle(mark_light, cx + s * 70, 152, 30); a.pill(glint, cx + s * 66, 188, 12, 5); } // Rings, Fn.
    }
    a.circle(glint, cx, 152, 5.5f); // PS
    for (int i = -2; i <= 2; ++i) a.circle(glint, cx + i * 6.0f, 118, 1.6f);
    for (int i = 0; i < 4; ++i) a.circle(glint, cx - 9 + i * 6.0f, 124, 1.6f);
    a.pill(glint, cx, 176, 14, 5); // Mute
}

void dualshock4(Builder& a) {
    a.mirrored(body, 26, {{50, 26, 100, 22, 132, 22}, {172, 22, 196, 40, 204, 70}, {212, 110, 216, 200, 208, 244},
                          {202, 276, 176, 284, 162, 270}, {148, 256, 136, 222, 112, 206}, {86, 192, 40, 196, 0, 196}});
    a.rect(screen, cx - 66, 22, cx + 66, 30, 4);              // Light bar along the top edge.
    a.line(mark_light, cx - 58, 26, cx + 58, 26);
    a.rect(screen, cx - 70, 35, cx + 70, 102, 5, 5, 16, 16);  // Touchpad.
    a.pill(key_dark, cx - 90, 42, 13, 6, 100); a.pill(key_dark, cx + 90, 42, 13, 6, 80); // Share / Options
    a.circle(well, cx - 128, 86, 37); a.arrows(key_light, cx - 128, 86, 8, 31, 8.5f);
    a.circle(well, cx + 128, 86, 42); a.diamond(key_light, cx + 128, 86, 28, 12.5f); a.playstation(cx + 128, 86, 28, 5.5f);
    a.stick(cx - 62, 152, 30, 23); a.stick(cx + 62, 152, 30, 23);
    a.circle(key_dark, cx, 150, 8);
    for (int i = -2; i <= 2; ++i) a.circle(dot, cx + i * 6.0f, 114, 1.6f);
    for (int i = 0; i < 4; ++i) a.circle(dot, cx - 9 + i * 6.0f, 120, 1.6f);
}

void switchPro(Builder& a) {
    a.mirrored(body, 22, {{50, 22, 100, 14, 130, 14}, {170, 14, 194, 32, 203, 64}, {213, 108, 221, 210, 217, 256},
                          {214, 290, 189, 298, 173, 284}, {158, 271, 140, 224, 112, 204}, {82, 184, 40, 188, 0, 188}});
    a.stick(cx - 110, 84, 33, 25);
    a.diamond(key_dark, cx + 110, 84, 27, 12);
    a.circle(well, cx - 56, 150, 30); a.cross(key_dark, cx - 56, 150, 22, 8);
    a.stick(cx + 56, 150, 30, 23);
    a.line(mark, cx - 46, 50, cx - 34, 50);                                          // Minus
    a.line(mark, cx + 34, 50, cx + 46, 50); a.line(mark, cx + 40, 44, cx + 40, 56);  // Plus
    a.rect(key_dark, cx - 37, 97, cx - 23, 111, 3); a.circle(mark_light, cx - 30, 104, 3.5f); // Capture
    a.circle(key_light, cx + 30, 104, 8); a.circle(mark, cx + 30, 104, 4);                     // Home
}

// One Joy-Con centred on x; the rail faces the other half of a pair.
void joycon(Builder& a, float x, bool left) {
    const float s = left ? 1.0f : -1.0f, face = x - s * 5, rail = x + s * 45;
    if (left) a.rect(body, x - 50, 16, x + 40, 284, 46, 5, 5, 46);
    else a.rect(body, x - 40, 16, x + 50, 284, 5, 46, 46, 5);
    a.rect(key_dark, rail - 5, 24, rail + 5, 276, 3);
    a.rect(glint, rail - 2, 66, rail + 2, 96, 2); a.rect(glint, rail - 2, 204, rail + 2, 234, 2); // SL / SR
    if (left) {
        a.line(mark, face + 17, 42, face + 27, 42);
        a.stick(face, 94, 26, 19);
        a.diamond(key_dark, face, 170, 20, 9.5f);
        a.rect(key_dark, face + 8, 229, face + 20, 241, 2.5f); // Capture
    } else {
        a.line(mark, face - 27, 42, face - 17, 42); a.line(mark, face - 22, 37, face - 22, 47);
        a.diamond(key_dark, face, 100, 20, 9.5f);
        a.stick(face, 180, 26, 19);
        a.circle(key_light, face - 14, 240, 7); a.circle(mark, face - 14, 240, 3.5f); // Home
    }
}

void ultimate(Builder& a, bool second) {
    a.mirrored(body, 30, {{60, 30, 118, 26, 150, 28}, {186, 30, 204, 50, 209, 84}, {215, 130, 217, 206, 211, 246},
                          {205, 280, 179, 288, 165, 272}, {151, 256, 140, 226, 116, 212}, {86, 198, 40, 200, 0, 200}});
    a.stick(cx - 112, 94, 32, 24);
    a.stick(cx + 56, 160, 31, 23);
    if (second) { a.circle(mark, cx - 112, 94, 28.5f); a.circle(mark, cx + 56, 160, 27.5f); } // Stick light rings.
    a.diamond(key_dark, cx + 112, 94, 26, 12);
    a.circle(well, cx - 56, 160, 32); a.cross(key_dark, cx - 56, 160, 24, 9);
    a.circle(key_light, cx - 40, 62, 7); a.line(mark, cx - 43.5f, 62, cx - 36.5f, 62);
    a.circle(key_light, cx + 40, 62, 7); a.line(mark, cx + 36.5f, 62, cx + 43.5f, 62); a.line(mark, cx + 40, 58.5f, cx + 40, 65.5f);
    a.circle(key_light, cx, 60, 11);
    a.star(dot, cx, 100, 8, 3.4f); // Profile star.
    if (second) { a.circle(key_light, cx - 22, 84, 5); a.circle(key_light, cx + 22, 84, 5); }
}

void eightbitdoPro(Builder& a) {
    a.mirrored(body, 40, {{60, 40, 120, 36, 150, 38}, {190, 40, 212, 70, 212, 110}, {212, 150, 206, 216, 196, 250},
                          {188, 280, 160, 286, 148, 266}, {138, 248, 128, 222, 106, 214}, {80, 206, 40, 210, 0, 210}});
    a.circle(well, cx - 142, 106, 33); a.cross(key_dark, cx - 142, 106, 25, 9);
    a.diamond(key_dark, cx + 142, 106, 26, 12);
    a.pill(key_dark, cx - 24, 104, 18, 7, -35); a.pill(key_dark, cx + 24, 104, 18, 7, -35); // Select / Start
    a.stick(cx - 58, 168, 28, 21); a.stick(cx + 58, 168, 28, 21);
    a.circle(key_light, cx, 68, 8);
    a.star(dot, cx, 136, 7, 3);
}

void eightbitdoSn(Builder& a) {
    a.mirrored(body, 82, {L(112, 82), {160, 76, 198, 104, 198, 152}, {198, 200, 160, 228, 112, 222}, {70, 216, 30, 216, 0, 216}});
    a.circle(well, cx - 122, 152, 36); a.cross(key_dark, cx - 122, 152, 27, 10);
    a.diamond(key_dark, cx + 122, 152, 28, 13);
    a.pill(key_dark, cx - 26, 160, 20, 8, -35); a.pill(key_dark, cx + 26, 160, 20, 8, -35);
}

void eightbitdoZero2(Builder& a) {
    // Pocket-sized capsule, white controls, no sticks or grips.
    a.pill(under, cx - 85, 62, 90, 14); a.pill(under, cx + 85, 62, 90, 14);
    a.rect(body, cx - 180, 65, cx + 180, 245, 87);
    a.cross(key_light, cx - 94, 155, 45, 15);
    a.diamond(key_light, cx + 94, 155, 36, 17);
    a.circle(key_light, cx - 20, 203, 14); a.circle(key_light, cx + 20, 203, 14);
    a.line(mark, cx - 25, 203, cx - 15, 203);
    a.line(mark, cx + 15, 203, cx + 25, 203); a.line(mark, cx + 20, 198, cx + 20, 208);
    a.line(mark, cx + 87, 112, cx + 101, 126); a.line(mark, cx + 101, 112, cx + 87, 126); // X
    a.polygon(mark, {{cx + 123, 162}, {cx + 130, 147}, {cx + 137, 162}}, false); // A
    a.line(mark, cx + 126, 157, cx + 134, 157);
    a.line(mark, cx + 51, 148, cx + 58, 155); a.line(mark, cx + 65, 148, cx + 58, 155); // Y
    a.line(mark, cx + 58, 155, cx + 58, 162);
}

void steamController(Builder& a) {
    a.mirrored(body, 40, {{40, 40, 80, 26, 116, 26}, {172, 26, 211, 58, 213, 110}, {215, 160, 213, 236, 197, 266},
                          {187, 288, 159, 292, 147, 270}, {137, 250, 120, 222, 92, 214}, {60, 206, 30, 212, 0, 212}});
    for (float s : {-1.0f, 1.0f}) { // Two large round trackpads.
        a.circle(well, cx + s * 104, 104, 56);
        a.circle(plate, cx + s * 104, 104, 49);
        a.circle(mark, cx + s * 104, 104, 31);
    }
    a.cross(mark, cx - 104, 104, 17, 5.5f);
    a.circle(dot, cx + 104, 104, 3);
    a.circle(key_light, cx, 110, 15); a.circle(mark, cx, 110, 8.5f);
    a.circle(key_light, cx - 34, 110, 6); a.polygon(mark, {{cx - 32, 107.5f}, {cx - 32, 112.5f}, {cx - 37, 110}});
    a.circle(key_light, cx + 34, 110, 6); a.polygon(mark, {{cx + 32, 107.5f}, {cx + 32, 112.5f}, {cx + 37, 110}});
    a.stick(cx - 44, 180, 23, 17);
    a.diamond(key_dark, cx + 44, 180, 16, 7.5f);
}

void steamController2(Builder& a) {
    // Triton: two sticks and two square trackpads below them, D-pad and ABXY above.
    a.mirrored(body, 22, {{50, 22, 100, 16, 140, 16}, {180, 16, 202, 44, 208, 86}, {214, 140, 216, 212, 208, 258},
                          {204, 284, 180, 294, 162, 280}, {148, 270, 132, 232, 108, 224}, {76, 216, 36, 222, 0, 222}});
    a.cross(key_dark, cx - 134, 70, 27, 10);
    a.diamond(key_dark, cx + 134, 70, 25, 12);
    a.stick(cx - 62, 106, 32, 24); a.stick(cx + 62, 106, 32, 24);
    for (float side : {-1.0f, 1.0f}) {
        const float x = cx + side * 86;
        a.rect(inlay, x - 52, 148, x + 52, 234, 17);
        a.rect(mark, x - 52, 148, x + 52, 234, 17);
        a.pill(key_dark, cx + side * 72, 36, 26, 12);
    }
    a.circle(key_dark, cx, 65, 14); a.circle(mark_light, cx, 65, 7);
    a.circle(glint, cx, 34, 2.5f);
    a.pill(key_dark, cx, 188, 33, 16);
    for (int i = -1; i <= 1; ++i) a.circle(mark_light, cx + i * 7.0f, 188, 1.7f);
}

void steamDeck(Builder& a) {
    a.mirrored(body, 68, {{60, 68, 120, 66, 160, 66}, {196, 66, 214, 82, 216, 110}, {218, 152, 218, 214, 206, 240},
                          {196, 262, 166, 264, 152, 248}, {144, 238, 130, 228, 110, 228}, L(0, 228)});
    a.rect(inlay, cx - 104, 74, cx + 104, 212, 10); // Bezel.
    a.rect(screen, cx - 92, 82, cx + 92, 197, 3);   // 16:10 display.
    for (float s : {-1.0f, 1.0f}) {
        a.stick(cx + s * 134, 102, 20, 15);
        a.rect(well, cx + s * 150 - 24, 150, cx + s * 150 + 24, 198, 9); // Square trackpads.
        a.pill(key_dark, cx + s * 180, 84, 10, 5);                       // View / Menu
    }
    a.cross(key_dark, cx - 180, 114, 14, 5);
    a.diamond(key_dark, cx + 180, 114, 13, 6);
    a.pill(key_dark, cx - 150, 216, 16, 6); // STEAM
    for (int i = -1; i <= 1; ++i) a.circle(dot, cx + 150 + i * 5.0f, 216, 1.8f); // Quick access
}

void logitech(Builder& a) {
    a.mirrored(body, 34, {{50, 34, 100, 24, 140, 24}, {180, 24, 204, 44, 208, 80}, {214, 124, 214, 200, 204, 240},
                          {196, 272, 168, 280, 156, 262}, {144, 244, 130, 214, 104, 206}, {74, 198, 36, 200, 0, 200}});
    a.circle(well, cx - 136, 98, 34); a.circle(key_light, cx - 136, 98, 29); a.cross(key_dark, cx - 136, 98, 21, 7.5f);
    a.circle(well, cx + 136, 98, 40); a.diamond(key_dark, cx + 136, 98, 26, 12);
    a.stick(cx - 60, 164, 29, 22); a.stick(cx + 60, 164, 29, 22);
    a.pill(key_dark, cx - 38, 92, 14, 6); a.pill(key_dark, cx + 38, 92, 14, 6); // Back / Start
    a.circle(key_dark, cx, 116, 11); a.circle(mark_light, cx, 116, 5.5f);       // Logitech button
    a.circle(key_dark, cx, 146, 5); a.circle(dot, cx + 13, 146, 2);             // Mode + LED
}

void generic(Builder& a) {
    a.mirrored(body, 44, {{50, 44, 100, 32, 138, 32}, {180, 32, 204, 56, 206, 92}, {210, 140, 208, 208, 196, 244},
                          {188, 270, 160, 276, 148, 258}, {136, 240, 124, 214, 100, 208}, {70, 200, 36, 202, 0, 202}});
    a.circle(well, cx - 130, 108, 31); a.cross(key_dark, cx - 130, 108, 22, 8);
    a.circle(well, cx + 130, 108, 36); a.diamond(key_dark, cx + 130, 108, 24, 11);
    a.stick(cx - 58, 166, 27, 20); a.stick(cx + 58, 166, 27, 20);
    a.pill(key_light, cx - 24, 104, 14, 7); a.pill(key_light, cx + 24, 104, 14, 7);
    a.circle(key_light, cx, 134, 8);
}

void build(Builder& a, devices::Picture picture) {
    using devices::Picture;
    switch (picture) {
    case Picture::dualsense: dualsense(a, false); break;
    case Picture::dualsense_edge: dualsense(a, true); break;
    case Picture::dualshock4: dualshock4(a); break;
    case Picture::xbox: xbox(a, false); break;
    case Picture::xbox_elite: xbox(a, true); break;
    case Picture::xbox360: xbox360(a); break;
    case Picture::xbox_adaptive: adaptive(a); break;
    case Picture::switch_pro: switchPro(a); break;
    case Picture::joycon_left: joycon(a, cx, true); break;
    case Picture::joycon_right: joycon(a, cx, false); break;
    case Picture::joycon_pair: joycon(a, cx - 58, true); joycon(a, cx + 58, false); break;
    case Picture::ultimate2: ultimate(a, true); break;
    case Picture::eightbitdo_ultimate: ultimate(a, false); break;
    case Picture::eightbitdo_pro: eightbitdoPro(a); break;
    case Picture::eightbitdo_sn: eightbitdoSn(a); break;
    case Picture::eightbitdo_zero2: eightbitdoZero2(a); break;
    case Picture::steam_controller: steamController(a); break;
    case Picture::steam_controller2: steamController2(a); break;
    case Picture::steam_deck: steamDeck(a); break;
    case Picture::logitech: logitech(a); break;
    default: generic(a); break;
    }
}
} // namespace

const Renderer::ControllerArt* Renderer::controllerArt(devices::Picture picture) {
    // Unknown and unclassified devices share the neutral pad.
    if (picture == devices::Picture::none || picture >= devices::Picture::count) picture = devices::Picture::generic;
    const int key = static_cast<int>(picture);
    auto found = controller_art_.find(key);
    if (found == controller_art_.end()) {
        ControllerArt art{};
        if (factory_) {
            Builder builder(factory_.Get());
            build(builder, picture);
            if (!builder.finish(art)) art = {};
        }
        found = controller_art_.emplace(key, std::move(art)).first; // A failed build is not retried every frame.
    }
    return found->second[controller_art::body] ? &found->second : nullptr;
}

void Renderer::drawControllerArt(devices::Picture picture, D2D1_RECT_F box) {
    namespace art = controller_art;
    const float s = std::min((box.right - box.left) / art::width, (box.bottom - box.top) / art::height);
    if (s <= 0) return;
    const auto* layers = controllerArt(picture);
    if (!layers) { // Geometry failed: the small generic glyph still marks the slot.
        const float size = std::min(box.right - box.left, box.bottom - box.top) * 0.7f;
        drawControllerIcon((box.left + box.right - size) * 0.5f, (box.top + box.bottom - size) * 0.5f, size, D2D1::ColorF(0.65f, 0.69f, 0.72f));
        return;
    }
    if (!controller_art_stroke_)
        factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                D2D1_LINE_JOIN_ROUND), nullptr, 0, controller_art_stroke_.GetAddressOf());
    D2D1_MATRIX_3X2_F previous;
    target_->GetTransform(&previous);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                          D2D1::Matrix3x2F::Translation((box.left + box.right - art::width * s) * 0.5f,
                                                        (box.top + box.bottom - art::height * s) * 0.5f) * previous);
    // Neutral greys with the panel's faint cool cast.
    const auto tone = [](float v) { return D2D1::ColorF(v * 0.985f, v * 0.992f, std::min(1.0f, v * 1.004f)); };
    const auto gradient = [&](D2D1_COLOR_F top, D2D1_COLOR_F bottom) {
        ComPtr<ID2D1LinearGradientBrush> result;
        ComPtr<ID2D1GradientStopCollection> stops;
        const D2D1_GRADIENT_STOP list[] = {{0, top}, {1, bottom}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(list, 2, stops.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, art::height}), stops.Get(), result.GetAddressOf());
        return result;
    };
    const bool dark_shell = picture == devices::Picture::steam_controller2;
    const bool yellow_shell = picture == devices::Picture::eightbitdo_zero2;
    const auto shell = yellow_shell ? gradient(D2D1::ColorF(0xffe98a), D2D1::ColorF(0xe7b900)) :
        gradient(tone(dark_shell ? 0.30f : 0.97f), tone(dark_shell ? 0.14f : 0.78f));
    const auto back = gradient(tone(0.64f), tone(0.44f));
    const float px = 1.0f / s; // Stroke widths stay in logical pixels at every size.
    const auto fill = [&](art::Layer layer, ID2D1Brush* paint, D2D1_COLOR_F color) {
        if (paint) { target_->FillGeometry((*layers)[layer].Get(), paint); return; }
        brush(color); target_->FillGeometry((*layers)[layer].Get(), solid_.Get());
    };
    const auto stroke = [&](art::Layer layer, D2D1_COLOR_F color, float thickness) {
        brush(color); target_->DrawGeometry((*layers)[layer].Get(), solid_.Get(), thickness * px, controller_art_stroke_.Get());
    };
    fill(art::under, back.Get(), tone(0.54f));      stroke(art::under, tone(0.34f), 1.1f);
    fill(art::body, shell.Get(), yellow_shell ? D2D1::ColorF(0xf5cf48) : tone(dark_shell ? 0.2f : 0.9f));
    stroke(art::body, yellow_shell ? D2D1::ColorF(0xb79128) : tone(dark_shell ? 0.36f : 0.58f), 1.1f);
    fill(art::well, nullptr, tone(0.79f));          stroke(art::well, tone(0.69f), 0.9f);
    fill(art::inlay, nullptr, tone(0.16f));
    fill(art::plate, nullptr, tone(0.95f));         stroke(art::plate, tone(0.62f), 1.1f);
    fill(art::screen, nullptr, tone(0.22f));
    fill(art::key_light, nullptr, tone(0.97f));     stroke(art::key_light, tone(0.52f), 1.1f);
    fill(art::key_dark, nullptr, tone(0.2f));       stroke(art::key_dark, tone(0.11f), 1.0f);
    fill(art::cap, nullptr, tone(0.29f));
    stroke(art::mark, tone(0.44f), 1.5f);
    stroke(art::mark_light, tone(0.7f), 1.3f);
    fill(art::dot, nullptr, tone(0.38f));
    fill(art::glint, nullptr, tone(0.8f));
    target_->SetTransform(previous);
}

#ifdef LAUNCHME_UI_TEST
bool Renderer::paintControllerGalleryForTest() {
    if (!target_) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0.015f, 0.019f, 0.025f));
    const auto size = target_->GetSize();
    constexpr int columns = 5;
    const int count = static_cast<int>(devices::Picture::count) - 1; // Every family; `none` draws as generic.
    const int rows = (count + columns - 1) / columns;
    const float cell_w = size.width / columns, cell_h = size.height / rows;
    bool built = true;
    for (int i = 0; i < count; ++i) {
        const auto picture = static_cast<devices::Picture>(i + 1);
        built = controllerArt(picture) != nullptr && built;
        const float x = (i % columns) * cell_w, y = (i / columns) * cell_h;
        drawControllerArt(picture, D2D1::RectF(x + 14, y + 14, x + cell_w - 14, y + cell_h - 14));
    }
    return SUCCEEDED(target_->EndDraw()) && built;
}
#endif
