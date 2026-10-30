// Her anime marks (manpu): drawn shapes around her head for the feeling she shows — an anger vein,
// a sweat drop, gloom lines, "!", "?", sparkles, music notes, a glint, hearts, "Zzz", a star.
// They are placed about her face (faces.txt; the caller's transform already moves it with her
// head), pop in when the feeling starts and fade after a few seconds, except sleeping and thinking,
// which last.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <array>
#include <cmath>
#include <functional>

using Microsoft::WRL::ComPtr;
using companion::Emotion;

namespace {
constexpr float pi = 3.14159265f;
using render_detail::smoothFade;
// Overshooting pop (ease-out back) for a mark appearing.
float pop(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    constexpr float c = 1.70158f;
    return 1 + (c + 1) * std::pow(t - 1, 3.0f) + c * std::pow(t - 1, 2.0f);
}
D2D1_POINT_2F operator+(D2D1_POINT_2F a, D2D1_POINT_2F b) { return {a.x + b.x, a.y + b.y}; }
D2D1_POINT_2F scaled(D2D1_POINT_2F p, float s) { return {p.x * s, p.y * s}; }
D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }
}

void Renderer::drawCompanionMarks(const avatar::Pose& pose, D2D1_POINT_2F face, float face_size, float alpha) {
    const auto emotion = pose.emotion;
    if (emotion == Emotion::cool || alpha <= 0.01f) return;
    const float age = pose.emotion_age, t = pose.time;
    const bool lasting = emotion == Emotion::sleepy || emotion == Emotion::thinking;
    const float shown = alpha * smoothFade(age / 0.18f) * (lasting ? 1.0f : 1 - smoothFade((age - 5.0f) / 1.2f));
    if (shown <= 0.01f) return;
    if (!companion_stroke_)
        factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                    nullptr, 0, companion_stroke_.GetAddressOf());
    // The marks were laid out on the old 557×475 close-up, whose face (230 px) sat at (280, 229): (u, v) are
    // fractions of that picture, measured from her face, so they keep their place beside her head on any framing.
    // One unit: a 300th of that picture's height (145.3 units per face).
    const float s = face_size / 145.3f;
    const auto at = [&](float u, float v) {
        return D2D1::Point2F(face.x + (u * 557 - 280) / 230 * face_size, face.y + (v * 475 - 229) / 230 * face_size);
    };
    const auto ink = [&](float opacity) { return rgba(0.07f, 0.07f, 0.1f, 0.85f * opacity * shown); };
    // A closed or open path built in place; null when Direct2D refuses.
    const auto path = [&](const std::function<void(ID2D1GeometrySink*)>& build) {
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(geometry.GetAddressOf())) || FAILED(geometry->Open(sink.GetAddressOf()))) return ComPtr<ID2D1PathGeometry>{};
        build(sink.Get());
        if (FAILED(sink->Close())) return ComPtr<ID2D1PathGeometry>{};
        return geometry;
    };
    // A filled shape with a dark rim, or a stroke over a wider dark one: readable on any backdrop.
    const auto fill = [&](ID2D1Geometry* shape, D2D1_COLOR_F color) {
        if (!shape) return;
        brush(ink(1)); target_->DrawGeometry(shape, solid_.Get(), 2.2f * s, companion_stroke_.Get());
        color.a *= shown; brush(color); target_->FillGeometry(shape, solid_.Get());
    };
    const auto stroke = [&](ID2D1Geometry* shape, D2D1_COLOR_F color, float width) {
        if (!shape) return;
        brush(ink(1)); target_->DrawGeometry(shape, solid_.Get(), (width + 2.4f) * s, companion_stroke_.Get());
        color.a *= shown; brush(color); target_->DrawGeometry(shape, solid_.Get(), width * s, companion_stroke_.Get());
    };
    // Shapes about a centre, `r` units in radius.
    const auto star = [&](D2D1_POINT_2F c, float r, int points, float inner, float turn) {
        return path([&](ID2D1GeometrySink* sink) {
            for (int i = 0; i < points * 2; ++i) {
                const float angle = turn + pi * i / points - pi / 2;
                const float radius = (i % 2 ? inner : 1.0f) * r * s;
                const auto p = D2D1::Point2F(c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius);
                if (i) sink->AddLine(p); else sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
    };
    const auto heart = [&](D2D1_POINT_2F c, float r) {
        const float k = r * s;
        return path([&](ID2D1GeometrySink* sink) {
            sink->BeginFigure(c + D2D1::Point2F(0, 0.9f * k), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(-1.3f * k, -0.1f * k), c + D2D1::Point2F(-0.7f * k, -1.1f * k), c + D2D1::Point2F(0, -0.45f * k)));
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(0.7f * k, -1.1f * k), c + D2D1::Point2F(1.3f * k, -0.1f * k), c + D2D1::Point2F(0, 0.9f * k)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
    };
    const auto loop = [&](float period, float phase) { return std::fmod(t / period + phase, 1.0f); }; // 0…1, repeating.

    // The later feelings borrow an earlier one's marks; a wave, a sip or a controller needs none.
    const Emotion marks = emotion == Emotion::love ? Emotion::shy : emotion == Emotion::laugh ? Emotion::smile : emotion == Emotion::cry ? Emotion::sad :
                          emotion == Emotion::confused ? Emotion::thinking : emotion == Emotion::determined ? Emotion::smug :
                          emotion == Emotion::gaming ? Emotion::excited : emotion == Emotion::wave || emotion == Emotion::drink ? Emotion::cool : emotion;
    switch (marks) {
    case Emotion::angry: { // An anger vein: four bent strokes about a pulsing centre.
        const auto c = at(0.8f, 0.12f);
        const float k = 11 * s * (pop(age / 0.35f) + 0.1f * std::sin(t * 9));
        const auto vein = path([&](ID2D1GeometrySink* sink) {
            static constexpr std::array<D2D1_POINT_2F, 4> quarters{{{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
            for (const auto [x, y] : quarters) {
                sink->BeginFigure(c + D2D1::Point2F(x * 0.28f * k, y * k), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(c + D2D1::Point2F(x * 0.28f * k, y * 0.28f * k), c + D2D1::Point2F(x * k, y * 0.28f * k)));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        });
        stroke(vein.Get(), rgba(0.93f, 0.14f, 0.18f, 1), 3.4f);
        break;
    }
    case Emotion::shock: { // "!" with two surprise strokes.
        const float k = pop(age / 0.3f);
        const auto c = at(0.82f, 0.1f);
        const auto bar = path([&](ID2D1GeometrySink* sink) {
            sink->BeginFigure(c + scaled({0, -12}, s * k), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(c + scaled({0, 3}, s * k));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            for (const float side : {-1.0f, 1.0f}) {
                sink->BeginFigure(c + scaled({side * 9, -10}, s * k), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(c + scaled({side * 14, -16}, s * k));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        });
        stroke(bar.Get(), rgba(1, 0.97f, 0.9f, 1), 4.2f * k);
        ComPtr<ID2D1EllipseGeometry> dot;
        if (SUCCEEDED(factory_->CreateEllipseGeometry(D2D1::Ellipse(c + scaled({0, 10}, s * k), 2.6f * s * k, 2.6f * s * k), dot.GetAddressOf())))
            fill(dot.Get(), rgba(1, 0.97f, 0.9f, 1));
        break;
    }
    case Emotion::fear: { // A sweat drop sliding down, again and again.
        const float phase = loop(1.8f, 0);
        const auto c = at(0.77f, 0.2f) + D2D1::Point2F(0, phase * 10 * s);
        const float k = 7.5f * s;
        const auto drop = path([&](ID2D1GeometrySink* sink) {
            sink->BeginFigure(c + D2D1::Point2F(0, -1.6f * k), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(0.5f * k, -0.7f * k), c + D2D1::Point2F(1.1f * k, 0), c + D2D1::Point2F(0.9f * k, 0.55f * k)));
            sink->AddArc(D2D1::ArcSegment(c + D2D1::Point2F(-0.9f * k, 0.55f * k), D2D1::SizeF(0.95f * k, 0.95f * k), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(-1.1f * k, 0), c + D2D1::Point2F(-0.5f * k, -0.7f * k), c + D2D1::Point2F(0, -1.6f * k)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
        const float fade = 1 - smoothFade((phase - 0.75f) / 0.25f);
        fill(drop.Get(), rgba(0.62f, 0.84f, 1, fade));
        brush(rgba(1, 1, 1, 0.85f * fade * shown));
        target_->FillEllipse(D2D1::Ellipse(c + D2D1::Point2F(-0.35f * k, 0.15f * k), 0.22f * k, 0.32f * k), solid_.Get());
        break;
    }
    case Emotion::sad: { // Gloom lines over the top of her head.
        const auto lines = path([&](ID2D1GeometrySink* sink) {
            for (int i = 0; i < 4; ++i) {
                const float u = 0.24f + i * 0.065f, wave = std::sin(t * 2 + i) * 1.5f * s;
                sink->BeginFigure(at(u, 0.04f + 0.015f * (i % 2)), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(at(u, 0.11f) + D2D1::Point2F(wave, 0), at(u, 0.19f - 0.02f * (i % 2))));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        });
        if (lines) { brush(rgba(0.3f, 0.32f, 0.62f, 0.55f * shown)); target_->DrawGeometry(lines.Get(), solid_.Get(), 2.6f * s, companion_stroke_.Get()); }
        break;
    }
    case Emotion::thinking: { // A bobbing "?".
        const auto c = at(0.83f, 0.1f) + D2D1::Point2F(0, std::sin(t * 2.4f) * 2.5f * s);
        const float k = 7 * s * pop(age / 0.35f);
        const auto mark = path([&](ID2D1GeometrySink* sink) {
            sink->BeginFigure(c + D2D1::Point2F(-0.9f * k, -0.7f * k), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(-0.9f * k, -1.9f * k), c + D2D1::Point2F(0.95f * k, -1.9f * k), c + D2D1::Point2F(0.95f * k, -0.75f * k)));
            sink->AddBezier(D2D1::BezierSegment(c + D2D1::Point2F(0.95f * k, 0), c + D2D1::Point2F(0, 0), c + D2D1::Point2F(0, 0.75f * k)));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        });
        stroke(mark.Get(), rgba(1, 1, 1, 1), 3.4f);
        ComPtr<ID2D1EllipseGeometry> dot;
        if (SUCCEEDED(factory_->CreateEllipseGeometry(D2D1::Ellipse(c + D2D1::Point2F(0, 1.7f * k), 0.32f * k, 0.32f * k), dot.GetAddressOf())))
            fill(dot.Get(), rgba(1, 1, 1, 1));
        break;
    }
    case Emotion::excited: // Twinkling sparkles around her.
        // Position (u, v), radius and twinkle phase of each sparkle.
        for (const auto [u, v, r, phase] : std::array<std::array<float, 4>, 3>{{{0.84f, 0.1f, 9, 0}, {0.15f, 0.17f, 7, 1.7f}, {0.89f, 0.33f, 6, 3.1f}}}) {
            const float twinkle = 0.55f + 0.45f * std::abs(std::sin(t * 3.6f + phase));
            const auto shape = star(at(u, v), r * twinkle * pop(age / 0.35f), 4, 0.3f, 0);
            fill(shape.Get(), rgba(1, 0.87f, 0.35f, 1));
        }
        break;
    case Emotion::smile: // Music notes floating up.
        for (int i = 0; i < 2; ++i) {
            const float phase = loop(2.4f, i * 0.5f);
            const auto c = at(0.82f + 0.05f * i, 0.26f) + D2D1::Point2F(std::sin(phase * pi * 2) * 3 * s, -phase * 26 * s);
            const float k = 6.5f * s, fade = std::sin(phase * pi);
            const auto note = path([&](ID2D1GeometrySink* sink) {
                sink->BeginFigure(c + D2D1::Point2F(0.9f * k, 0), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddArc(D2D1::ArcSegment(c + D2D1::Point2F(-0.9f * k, 0), D2D1::SizeF(0.9f * k, 0.65f * k), -20, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddArc(D2D1::ArcSegment(c + D2D1::Point2F(0.9f * k, 0), D2D1::SizeF(0.9f * k, 0.65f * k), -20, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddLine(c + D2D1::Point2F(0.9f * k, -2.6f * k));
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(c + D2D1::Point2F(1.9f * k, -2.0f * k), c + D2D1::Point2F(1.8f * k, -1.1f * k)));
                sink->AddLine(c + D2D1::Point2F(1.05f * k, -1.7f * k));
                sink->AddLine(c + D2D1::Point2F(1.05f * k, 0));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            });
            fill(note.Get(), rgba(1, 0.62f, 0.78f, fade));
        }
        break;
    case Emotion::smug: { // A slow glint.
        const auto shape = star(at(0.8f, 0.15f), 9 * pop(age / 0.35f) * (0.8f + 0.2f * std::sin(t * 2.2f)), 4, 0.22f, t * 0.6f);
        fill(shape.Get(), rgba(1, 1, 0.92f, 1));
        break;
    }
    case Emotion::shy: // Little hearts rising.
        for (int i = 0; i < 2; ++i) {
            const float phase = loop(2.2f, i * 0.5f);
            const auto shape = heart(at(0.8f + 0.06f * i, 0.3f) + D2D1::Point2F(std::sin(phase * pi * 2 + i) * 3 * s, -phase * 24 * s), 6.0f + 2.0f * static_cast<float>(i));
            fill(shape.Get(), rgba(1, 0.45f, 0.6f, std::sin(phase * pi)));
        }
        break;
    case Emotion::playful: { // A popping star.
        const auto shape = star(at(0.82f, 0.15f), 9 * pop(age / 0.35f) * (0.9f + 0.1f * std::sin(t * 5)), 5, 0.45f, std::sin(t * 1.5f) * 0.2f);
        fill(shape.Get(), rgba(1, 0.84f, 0.3f, 1));
        break;
    }
    case Emotion::sleepy: // "Z z z" drifting up and away.
        for (int i = 0; i < 3; ++i) {
            const float phase = loop(3.0f, i / 3.0f);
            const auto c = at(0.72f, 0.18f) + D2D1::Point2F(phase * 22 * s, -phase * 26 * s);
            const float k = (4.5f + phase * 5.5f) * s;
            const auto z = path([&](ID2D1GeometrySink* sink) {
                sink->BeginFigure(c + D2D1::Point2F(-k, -k), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(c + D2D1::Point2F(k, -k));
                sink->AddLine(c + D2D1::Point2F(-k, k));
                sink->AddLine(c + D2D1::Point2F(k, k));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            });
            stroke(z.Get(), rgba(0.85f, 0.9f, 1, std::sin(phase * pi)), 2.2f);
        }
        break;
    default: break; // Calm.
    }
}
