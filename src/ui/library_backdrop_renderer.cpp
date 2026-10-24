// All games: large, soft light trails around a fixed centre. Procedural Direct2D
// only; the existing ambient clock pauses the movement when idle.
#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float tau = 2.0f * std::numbers::pi_v<float>;
D2D1_COLOR_F color(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
// Blue/jade/cyan, amber/coral, rose, then lavender/violet. Three related shades
// keep the ribbons distinct while the whole scene changes temperature together.
constexpr D2D1_COLOR_F palettes[][3] = {
    {{0.32f, 0.48f, 0.96f, 1}, {0.43f, 0.91f, 0.57f, 1}, {0.34f, 0.82f, 0.94f, 1}},
    {{1.00f, 0.74f, 0.35f, 1}, {1.00f, 0.49f, 0.23f, 1}, {0.98f, 0.60f, 0.43f, 1}},
    {{0.89f, 0.47f, 0.76f, 1}, {1.00f, 0.52f, 0.65f, 1}, {0.97f, 0.67f, 0.82f, 1}},
    {{0.49f, 0.49f, 0.97f, 1}, {0.73f, 0.48f, 0.96f, 1}, {0.64f, 0.66f, 1.00f, 1}}
};
}

void Renderer::drawLibraryBackdrop(float opacity) {
    const float width = viewport_.width, height = viewport_.height;
    const float scale = height / 900.0f;
    const float time = elapsed_; // No wall-clock jump after a game or the idle pause.
    const auto bounds = D2D1::RectF(0, 0, width, height);
    brush(color(0.015f, 0.025f, 0.055f, opacity));
    target_->FillRectangle(bounds, solid_.Get());

    // Six seconds at each palette, then an eight-second ease into the next one.
    // The ambient clock also freezes the colour transition when the launcher idles.
    constexpr float hold = 6, transition = 8, period = hold + transition;
    const float cycle = std::fmod(std::max(time, 0.0f), period * static_cast<float>(std::size(palettes)));
    const size_t current = static_cast<size_t>(cycle / period), next = (current + 1) % std::size(palettes);
    const float progress = std::clamp((cycle - static_cast<float>(current) * period - hold) / transition, 0.0f, 1.0f);
    const float blend = progress * progress * (3 - 2 * progress);

    // Gradient stops are immutable. Crossfade cached palette brushes rather than
    // allocating three new gradient resources on every animation frame.
    static_assert(std::size(palettes) * std::size(palettes[0]) == std::tuple_size_v<decltype(library_glows_)>);
    const auto eachLight = [&](size_t index, const auto& draw) {
        for (int pass = 0; pass < 2; ++pass) {
            const float weight = pass ? blend : 1 - blend;
            if (weight <= 0) continue;
            const size_t palette = pass ? next : current;
            auto& light = library_glows_[palette * std::size(palettes[0]) + index];
            if (!light) {
                const auto c = palettes[palette][index];
                const D2D1_GRADIENT_STOP data[] = {
                    {0, c}, {0.18f, color(c.r, c.g, c.b, 0.65f)},
                    {0.48f, color(c.r, c.g, c.b, 0.19f)}, {1, color(c.r, c.g, c.b, 0)}};
                ComPtr<ID2D1GradientStopCollection> stops;
                if (SUCCEEDED(target_->CreateGradientStopCollection(data, 4, stops.GetAddressOf())))
                    target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1),
                                                       stops.Get(), light.GetAddressOf());
            }
            if (light) draw(light.Get(), weight);
        }
    };
    const auto glow = [&](size_t index, float x, float y, float rx, float ry, float alpha) {
        eachLight(index, [&](ID2D1RadialGradientBrush* light, float weight) {
            light->SetCenter({x, y}); light->SetRadiusX(rx); light->SetRadiusY(ry);
            light->SetOpacity(alpha * opacity * weight);
            target_->FillRectangle(D2D1::RectF(x - rx, y - ry, x + rx, y + ry), light);
        });
    };
    const float cx = width * 0.5f, cy = height * 0.5f;
    const float radius = std::min(width * 0.38f, height * 0.69f);

    // Each ribbon is one closed, tangent-continuous Bezier path: no segmented
    // strokes, corners or bright joins. A radial brush lights the curve near its orb.
    constexpr int samples = 32;
    for (int ribbon = 0; ribbon < 3; ++ribbon) {
        const float phase = ribbon * tau / 3;
        const float tilt = -0.22f + ribbon * 0.20f + std::sin(time * 0.08f + phase) * 0.13f;
        const float ct = std::cos(tilt), st = std::sin(tilt);
        const float rx = radius * (1.0f - ribbon * 0.055f);
        const float ry = radius * (0.34f + ribbon * 0.065f);
        const auto point = [&](float angle) -> D2D1_POINT_2F {
            const float x = rx * (std::cos(angle) + 0.025f * std::cos(3 * angle + time * 0.09f + phase));
            const float y = ry * (std::sin(angle) + 0.09f * std::sin(2 * angle - time * 0.12f + phase));
            return {cx + x * ct - y * st, cy + x * st + y * ct};
        };
        std::array<D2D1_POINT_2F, samples> points;
        for (int i = 0; i < samples; ++i) points[i] = point(tau * i / samples);
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) continue;
        sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 0; i < samples; ++i) {
            const auto a = points[(i + samples - 1) % samples], b = points[i];
            const auto c = points[(i + 1) % samples], d = points[(i + 2) % samples];
            sink->AddBezier(D2D1::BezierSegment({b.x + (c.x - a.x) / 6, b.y + (c.y - a.y) / 6},
                                               {c.x - (d.x - b.x) / 6, c.y - (d.y - b.y) / 6}, c));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) continue;
        const size_t light_index = ribbon == 0 ? 1 : ribbon == 1 ? 2 : 0;
        const auto from = palettes[current][light_index], to = palettes[next][light_index];
        const auto ink = color(std::lerp(from.r, to.r, blend), std::lerp(from.g, to.g, blend), std::lerp(from.b, to.b, blend));
        const auto stroke = [&](float thickness, float alpha) {
            brush(color(ink.r, ink.g, ink.b, alpha * opacity));
            target_->DrawGeometry(path.Get(), solid_.Get(), thickness * scale);
        };
        stroke(22, 0.012f);
        stroke(11, 0.030f);
        stroke(5.5f, 0.065f);
        stroke(2.6f, 0.28f);

        const auto head = point(time * (0.18f + ribbon * 0.018f) + phase - 0.6f);
        eachLight(light_index, [&](ID2D1RadialGradientBrush* light, float weight) {
            light->SetCenter(head); light->SetRadiusX(radius * 0.42f); light->SetRadiusY(radius * 0.42f);
            light->SetOpacity(opacity * weight * 0.10f);
            target_->DrawGeometry(path.Get(), light, 14 * scale);
            light->SetOpacity(opacity * weight * 0.28f);
            target_->DrawGeometry(path.Get(), light, 6 * scale);
            light->SetOpacity(opacity * weight * 0.95f);
            target_->DrawGeometry(path.Get(), light, 2.8f * scale);
        });
        glow(light_index, head.x, head.y, 72 * scale, 72 * scale, 0.42f);
        glow(light_index, head.x, head.y, 22 * scale, 22 * scale, 0.78f);
        brush(color(0.86f + ink.r * 0.14f, 0.94f + ink.g * 0.06f, 0.89f + ink.b * 0.11f, opacity * 0.96f));
        target_->FillEllipse(D2D1::Ellipse(head, 5.2f * scale, 5.2f * scale), solid_.Get());
    }
}
