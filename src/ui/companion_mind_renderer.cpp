// Towarzyszka's "Umysł i pamięć" (her chat's "…" menu): the holographic sphere of her memories on
// the left — it builds itself as the screen opens: the core ignites, ribbons draw, embers fly out,
// her memories pop in shell by shell and threads reach them — and one column of cards on the right
// (her profile, four counters, the selected memory over the list of all of them). Her hidden
// thoughts are masked there like spoilers until the player shows one (Pokaż). Geometry:
// companion_ui::Layout (companion_view.hpp); the nodes and her telemetry: src/app/companion_mind_view.cpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "../net/companion_memory.hpp"

#include <array>
#include <cmath>
#include <cwctype>
#include <initializer_list>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
float clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }
float span(float t, float from, float to) { return clamp01((t - from) / (to - from)); }
float easeOut(float t) { const float u = 1 - clamp01(t); return 1 - u * u * u; }
float easeInOut(float t) { t = clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) * 0.5f; }
// A pop: past its size a little, then settled.
float easeBack(float t) {
    t = clamp01(t);
    constexpr float c1 = 1.70158f, c3 = c1 + 1;
    const float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
float fract(float v) { return v - std::floor(v); }
float hash01(float v) { return fract(std::sin(v * 12.9898f) * 43758.5453f); }
D2D1_COLOR_F affinityPink(float alpha) { return D2D1::ColorF(1.0f, 0.47f, 0.64f, alpha); }
bool tagChar(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L':'; }
// Her words as they read: emotion and state tags ([smug], [zycie:chora]) left out, game links
// ([[Title]]) as their titles, spaces tidied. Display only: the nodes keep their stored text.
std::wstring readable(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == L'[' && i + 1 < text.size() && text[i + 1] == L'[') {
            const auto close = text.find(L"]]", i + 2);
            if (close == std::wstring_view::npos) { i += 2; continue; }
            out.append(text.substr(i + 2, close - i - 2));
            i = close + 2;
            continue;
        }
        if (text[i] == L'[') {
            size_t j = i + 1;
            while (j < text.size() && j - i <= 28 && tagChar(text[j])) ++j;
            if (j < text.size() && text[j] == L']' && j > i + 1) { i = j + 1; continue; }
            if (j > i + 1 && (j == text.size() || text[j] == L'\u2026')) { // A tag cut short at the end ("[exc…").
                if (j < text.size()) out += L'\u2026';
                break;
            }
        }
        out += text[i++];
    }
    std::wstring tidy;
    tidy.reserve(out.size());
    for (const wchar_t c : out) {
        if (c == L' ' && (tidy.empty() || tidy.back() == L' ')) continue;
        tidy += c;
    }
    while (!tidy.empty() && tidy.back() == L' ') tidy.pop_back();
    return tidy;
}
uint32_t seedOf(std::wstring_view text) { return static_cast<uint32_t>(std::hash<std::wstring_view>{}(text)); }
} // namespace

float Renderer::drawMaskedText(const std::wstring& value, float x, float top, float width, IDWriteTextFormat* format, int max_lines,
                               D2D1_COLOR_F ink, float alpha, uint32_t seed) {
    if (value.empty() || width <= 4 || max_lines <= 0 || !format) return 0;
    const auto metrics = typeMetrics(format);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, width, metrics.line * max_lines + 0.5f,
                                         layout.GetAddressOf()))) return 0;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TEXT_METRICS measured{};
    if (FAILED(layout->GetMetrics(&measured))) return 0;
    const int lines = std::clamp(static_cast<int>(measured.lineCount), 1, max_lines);
    if (alpha <= 0.003f) return lines * metrics.line;
    const float bar = std::max(8.0f, metrics.cap * 1.34f), limit = metrics.line * lines + 0.5f;
    // A soft light sweeps across now and then; each paragraph a little out of step with the others.
    const float sweep = fract(elapsed_ * 0.42f + static_cast<float>(seed % 101) * 0.0097f);
    const float band = x - 120 + sweep * (width + 240);
    const auto grain = D2D1::ColorF(0.62f + 0.38f * ink.r, 0.62f + 0.38f * ink.g, 0.62f + 0.38f * ink.b, 1);
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    uint32_t word = 0;
    for (size_t at = 0; at < value.size();) {
        while (at < value.size() && std::iswspace(value[at])) ++at;
        size_t end = at;
        while (end < value.size() && !std::iswspace(value[end])) ++end;
        if (end <= at) break;
        UINT32 count = 0;
        layout->HitTestTextRange(static_cast<UINT32>(at), static_cast<UINT32>(end - at), 0, 0, nullptr, 0, &count);
        hits.resize(count);
        if (count && SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(at), static_cast<UINT32>(end - at), 0, 0, hits.data(), count, &count))) {
            for (UINT32 h = 0; h < count; ++h) {
                const auto& hit = hits[h];
                if (hit.top + hit.height > limit) continue; // Past the last line shown.
                const float left = x + hit.left + 1.5f, right = std::min(x + width, x + hit.left + hit.width) - 1.5f;
                if (right - left < 3) continue;
                const float cy = top + hit.top + metrics.baseline - metrics.cap * 0.5f;
                const auto rect = D2D1::RectF(left, cy - bar * 0.5f, right, cy + bar * 0.5f);
                const float d = ((left + right) * 0.5f - band) / 90.0f;
                const float lit = std::exp(-d * d);
                brush(D2D1::ColorF(ink.r, ink.g, ink.b, alpha * (0.22f + 0.24f * lit)));
                target_->FillRoundedRectangle(D2D1::RoundedRect(rect, bar * 0.5f, bar * 0.5f), solid_.Get());
                // Grain inside: specks that twinkle and drift a little, like a veil.
                const int specks = std::min(60, std::max(2, static_cast<int>((right - left) / 4.5f)));
                for (int k = 0; k < specks; ++k) {
                    uint32_t mix = seed * 2654435761u ^ (word * 40503u + static_cast<uint32_t>(k) * 2246822519u + h * 97u);
                    mix ^= mix >> 15; mix *= 2246822519u; mix ^= mix >> 13; mix *= 3266489917u; mix ^= mix >> 16;
                    const float fx = static_cast<float>(mix & 0xffff) / 65535.0f, fy = static_cast<float>((mix >> 16) & 0xff) / 255.0f;
                    const float fp = static_cast<float>((mix >> 24) & 0xff) / 255.0f;
                    const float twinkle = 0.5f + 0.5f * std::sin(elapsed_ * (1.4f + 2.6f * fp) + fp * 6.2832f);
                    const float px = left + 2 + fx * (right - left - 4) + std::sin(elapsed_ * 0.8f + fp * 9.0f) * 1.6f;
                    const float py = cy + (fy - 0.5f) * bar * 0.66f;
                    auto speck = grain;
                    speck.a = std::min(1.0f, alpha * (0.14f + 0.7f * twinkle * twinkle * twinkle + 0.25f * lit));
                    brush(speck);
                    const float size = 0.8f + 0.7f * fp * fp;
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(std::clamp(px, left + 1, right - 1), py), size, size), solid_.Get());
                }
            }
        }
        ++word;
        at = end;
    }
    return lines * metrics.line;
}

void Renderer::capsLine(const std::wstring& value, float x, float center_y, float width, D2D1_COLOR_F ink, DWRITE_TEXT_ALIGNMENT alignment) {
    auto* format = label_font_.Get();
    if (value.empty() || width <= 0 || !format) return;
    const bool english = i18n::english();
    std::wstring upper(value.size() * 2 + 8, L'\0');
    const int written = LCMapStringEx(english ? L"en-US" : L"pl-PL", LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, value.c_str(), static_cast<int>(value.size()),
                                      upper.data(), static_cast<int>(upper.size()), nullptr, nullptr, 0);
    if (written > 0) upper.resize(static_cast<size_t>(written)); else upper = value;
    const auto metrics = typeMetrics(format);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(upper.c_str(), static_cast<UINT32>(upper.size()), format, width, metrics.line, layout.GetAddressOf()))) return;
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    layout->SetTextAlignment(alignment);
    ComPtr<IDWriteTextLayout1> spaced;
    if (SUCCEEDED(layout.As(&spaced))) spaced->SetCharacterSpacing(0, 1.1f, 0, {0, static_cast<UINT32>(upper.size())});
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(x, center_y - (metrics.baseline - metrics.cap * 0.5f)), layout.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::drawCompanionMind(const companion_ui::Layout& layout, const companion_ui::Chat& chat, float shown) {
    const auto* view = context_->companion;
    if (!view) return;
    using MindLayout = companion_ui::Layout;
    using Kind = CompanionView::MindNode::Kind;
    const float base_alpha = clamp01(shown);
    float alpha = base_alpha; // A card coming in scales it for a moment (holo, charColor and the cards follow it).
    const auto tint = companion_ui::tint(view->look);
    // The build: 0 as the screen opens … 1 after mind_build_seconds; each part has its stretch of it.
    const float build = clamp01(chat.mind_age / MindLayout::mind_build_seconds);
    const auto stage = [&](float from, float to) { return span(build, from, to); };
    // Her hologram's palette: a deep shade of her colour -> her colour -> a luminous pastel white at high heat.
    const auto holo = [&](float a, float heat = 0.5f) {
        const float h = clamp01(heat);
        float r = 0, g = 0, b = 0;
        if (h < 0.55f) {
            const float t = h / 0.55f;
            r = tint.r * (0.28f + 0.72f * t);
            g = tint.g * (0.24f + 0.76f * t);
            b = tint.b * (0.32f + 0.68f * t);
        } else {
            const float t = (h - 0.55f) / 0.45f;
            r = tint.r + (1.0f - tint.r) * (t * 0.88f);
            g = tint.g + (0.98f - tint.g) * (t * 0.85f);
            b = tint.b + (1.0f - tint.b) * (t * 0.88f);
        }
        return D2D1::ColorF(clamp01(r), clamp01(g), clamp01(b), a * alpha);
    };
    const auto charColor = [&](float a) { return D2D1::ColorF(0.38f + 0.62f * tint.r, 0.38f + 0.62f * tint.g, 0.38f + 0.62f * tint.b, a * alpha); };
    const auto radial = [&](D2D1_POINT_2F center, float r, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1RadialGradientBrush> fill;
        if (r <= 0.5f || FAILED(target_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), collection.GetAddressOf())) ||
            FAILED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center, {}, r, r), collection.Get(), fill.GetAddressOf()))) return;
        target_->FillEllipse(D2D1::Ellipse(center, r, r), fill.Get());
    };
    const D2D1_COLOR_F clear = D2D1::ColorF(0, 0, 0, 0);
    if (!companion_stroke_)
        factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                    nullptr, 0, companion_stroke_.GetAddressOf());

    // Frosted backdrop over the chat, in Launchme's neutral dark slate.
    {
        const D2D1_RECT_F screen = D2D1::RectF(0, 0, viewport_.width, viewport_.height);
        ComPtr<ID2D1RectangleGeometry> screen_geo;
        if (glass_level_ > 0.01f && SUCCEEDED(factory_->CreateRectangleGeometry(screen, screen_geo.GetAddressOf())) && frostCapture(screen_geo.Get(), 32.0f))
            frostDraw(screen_geo.Get(), alpha);
        brush(D2D1::ColorF(0.02f, 0.022f, 0.027f, 0.9f * alpha));
        target_->FillRectangle(screen, solid_.Get());
    }

    const auto sphere_rect = box(layout.mindSphere());
    const auto panel_rect = box(layout.mindPanel());
    const float cx = (sphere_rect.left + sphere_rect.right) * 0.5f;
    const float cy = (sphere_rect.top + sphere_rect.bottom) * 0.5f;
    const float grow = easeOut(stage(0.0f, 0.5f));
    // The wheel's zoom (100–200%): the sphere grows about its centre and what is on it grows a little too.
    const float zoom = std::clamp(chat.mind_zoom_spring.value, 1.0f, MindLayout::mind_zoom_max);
    const float magnify = std::sqrt(zoom);
    const float radius = layout.mindRadius(zoom) * (0.94f + 0.06f * alpha) * (0.8f + 0.2f * grow);
    // Zoomed past its column, it fades out before the cards, the header and the hints.
    const bool zoomed = zoom > 1.002f;
    const auto inside = [&](float x, float y) { return x < panel_rect.left - 24 && y > MindLayout::mind_top - 20 && y < viewport_.height - 40; };

    // The chat's header row, in place: Back, her name in her light with the screen's name after it,
    // and her connection on the right (her persona is on her card below).
    {
        const auto back = box(layout.mindClose());
        drawButtonGlass(back, alpha, false, chat.mind_hover == companion_ui::focus_back);
        const auto ink = buttonInk(0, true, alpha);
        const float head_y = (back.top + back.bottom) * 0.5f;
        line(back.left + 22, head_y, back.left + 34, head_y, ink, 2);
        line(back.left + 22, head_y, back.left + 28, head_y - 6, ink, 2);
        line(back.left + 22, head_y, back.left + 28, head_y + 6, ink, 2);
        auto* heading = settings_heading_font_.Get();
        const float title_x = back.right + 26, title_right = panel_rect.left - 12;
        textLine(view->name, title_x, head_y, title_right - title_x, heading, D2D1::ColorF(0.5f + 0.5f * tint.r, 0.5f + 0.5f * tint.g, 0.5f + 0.5f * tint.b, alpha));
        const float after_name = title_x + textAdvance(view->name, heading);
        textLine(std::wstring(L" · ") + tr(L"Umysł i pamięć"), after_name, head_y, title_right - after_name, heading, gray(0.6f, alpha));
        if (!view->status.empty())
            textLine(view->status, panel_rect.left, head_y, panel_rect.right - panel_rect.left, friend_status_font_.Get(),
                     view->chat && view->chat->offline ? palette::danger(0.9f * alpha) : palette::muted(alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // Zoomed: everything of the sphere goes through two soft masks (its column's right edge; above
    for (int i = 0; i < companion_ui::mind_filter_count; ++i) {
        const auto r = box(layout.mindFilter(i));
        const bool selected = static_cast<int>(chat.mind_filter) == i;
        const bool hovered = chat.mind_hover == companion_ui::mind_filter_hit_base - i;
        brush(selected ? charColor(0.14f) : gray(1, hovered ? 0.09f * alpha : 0.035f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, 12, 12), solid_.Get());
        if (selected) { brush(charColor(0.55f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(r, 12, 12), solid_.Get(), 1); }
        textLine(companion_ui::mindFilterLabel(static_cast<companion_ui::MindFilter>(i)), r.left + 8, (r.top + r.bottom) / 2, r.right - r.left - 16,
                 settings_body_font_.Get(), selected ? charColor(1) : gray(0.62f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Zoomed: everything of the sphere goes through two soft masks (its column's right edge; above
    // and below), so it melts away before the cards instead of being cut.
    int faded_layers = 0;
    if (zoomed) {
        const auto fade = [&](D2D1_POINT_2F from, D2D1_POINT_2F to, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> mask;
            if (FAILED(target_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), collection.GetAddressOf())) ||
                FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), collection.Get(), mask.GetAddressOf()))) return;
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f, mask.Get()), nullptr);
            ++faded_layers;
        };
        const D2D1_COLOR_F on = D2D1::ColorF(0, 0, 0, 1), off = D2D1::ColorF(0, 0, 0, 0);
        fade(D2D1::Point2F(panel_rect.left - 110, 0), D2D1::Point2F(panel_rect.left - 14, 0), {{0.0f, on}, {1.0f, off}});
        const float top = MindLayout::mind_top - 14, bottom = viewport_.height - 56, h = bottom - top;
        fade(D2D1::Point2F(0, top), D2D1::Point2F(0, bottom), {{0.0f, off}, {std::clamp(64.0f / h, 0.0f, 0.4f), on},
                                                                {std::clamp(1 - 96.0f / h, 0.6f, 1.0f), on}, {1.0f, off}});
    }

    // Eclipsed dark hemisphere behind the hologram, in her colour, swelling in with the build.
    radial(D2D1::Point2F(cx - radius * 0.06f, cy - radius * 0.05f), radius * 1.2f,
           {{0.0f, holo(0.22f * grow, 0.35f)}, {0.68f, holo(0.11f * grow, 0.15f)}, {0.93f, holo(0.09f * grow, 0.55f)}, {1.0f, clear}});

    const float yaw = chat.mind_yaw_spring.value;
    const float pitch = chat.mind_pitch_spring.value;
    const float cyaw = std::cos(yaw), syaw = std::sin(yaw);
    const float cpitch = std::cos(pitch), spitch = std::sin(pitch);
    const auto project = [&](float x, float y, float z, float& sx, float& sy, float& rz, float& scale) {
        const float rx = x * cyaw - z * syaw;
        const float rz0 = x * syaw + z * cyaw;
        const float ry = y * cpitch - rz0 * spitch;
        rz = y * spitch + rz0 * cpitch;
        const float perspective = 2.4f / (2.4f + rz * 0.65f);
        sx = cx + rx * radius * perspective;
        sy = cy - ry * radius * perspective;
        scale = perspective * magnify; // Sizes of what is drawn there.
    };

    // Additive light from here on: the particle cloud, ribbons, combs, threads and stars.
    ComPtr<ID2D1DeviceContext> context;
    target_.As(&context);
    if (context) context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);

    // 0. Rim light: a soft halo just outside her sphere's edge.
    radial(D2D1::Point2F(cx, cy), radius * 1.34f,
           {{0.0f, clear}, {0.64f, clear}, {0.73f, holo(0.12f * grow, 0.62f)}, {0.8f, holo(0.05f * grow, 0.5f)}, {1.0f, clear}});

    // 1. The core: it ignites with a flash first, then breathes.
    {
        const float ignite = stage(0.0f, 0.3f);
        const float flash = std::sin(3.14159265f * stage(0.02f, 0.42f));
        const float breathe = 1.0f + 0.05f * std::sin(elapsed_ * 2.2f);
        const float core_r = radius * 0.86f * breathe * (0.16f + 0.84f * easeBack(ignite));
        radial(D2D1::Point2F(cx, cy), core_r,
               {{0.0f, holo(0.92f, 0.98f)}, {0.1f, holo(0.62f, 0.88f)}, {0.3f, holo(0.26f, 0.62f)}, {0.62f, holo(0.08f, 0.36f)}, {1.0f, clear}});
        if (flash > 0.01f)
            radial(D2D1::Point2F(cx, cy), radius * (0.3f + 0.55f * ignite),
                   {{0.0f, D2D1::ColorF(1, 1, 1, 0.75f * flash * alpha)}, {0.35f, holo(0.4f * flash, 0.9f)}, {1.0f, clear}});
        brush(D2D1::ColorF(1, 1, 1, 0.9f * alpha * std::min(1.0f, ignite * 3)));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.03f, radius * 0.03f), solid_.Get());
    }

    // 2. Plasma ribbons around the nucleus: each draws itself in, then swirls (soft bloom, a body and a bright filament).
    for (int rib = 0; rib < 5; ++rib) {
        const float drawn = easeInOut(stage(0.06f + rib * 0.05f, 0.5f + rib * 0.05f));
        if (drawn <= 0.001f) continue;
        const float r_base = rib < 2 ? 0.22f + rib * 0.09f : 0.48f + (rib - 2) * 0.16f;
        const float tilt_a = rib * 1.12f + elapsed_ * (rib % 2 == 0 ? 0.22f : -0.17f);
        const float tilt_b = rib * 0.74f - 0.4f;
        const float ca = std::cos(tilt_a), sa = std::sin(tilt_a);
        const float cb = std::cos(tilt_b), sb = std::sin(tilt_b);
        const bool inner = rib < 2;
        constexpr int steps = 72;
        D2D1_POINT_2F prev{};
        bool has_prev = false;
        float head_depth = 0;
        for (int s = 0; s <= steps; ++s) {
            const float t = static_cast<float>(s) / steps;
            if (t > drawn) break;
            if (std::sin(t * 6.2831853f * 2.0f + rib * 1.7f) < -0.35f) { has_prev = false; continue; }
            const float ang = t * 6.2831853f;
            const float wobble = 1.0f + 0.12f * std::sin(ang * 3.0f + elapsed_ * 1.2f);
            const float x0 = r_base * wobble * std::cos(ang);
            const float z0 = r_base * wobble * std::sin(ang);
            const float y0 = 0.08f * r_base * std::sin(ang * 2.0f);
            const float y1 = y0 * ca - z0 * sa, z1 = y0 * sa + z0 * ca;
            const float x2 = x0 * cb - y1 * sb, y2 = x0 * sb + y1 * cb;
            float sx = 0, sy = 0, rz = 0, sc = 1;
            project(x2, y2, z1, sx, sy, rz, sc);
            const D2D1_POINT_2F pt = D2D1::Point2F(sx, sy);
            const float depth = std::clamp(1.0f - (rz + 1.0f) * 0.38f, 0.15f, 1.0f);
            if (has_prev) {
                const float heat = inner ? 0.85f : 0.5f;
                brush(holo(0.05f * depth, heat * 0.7f));
                target_->DrawLine(prev, pt, solid_.Get(), (inner ? 10.0f : 7.0f) * sc);
                brush(holo(0.1f * depth, heat * 0.85f));
                target_->DrawLine(prev, pt, solid_.Get(), (inner ? 4.2f : 3.0f) * sc);
                brush(holo(0.34f * depth, heat));
                target_->DrawLine(prev, pt, solid_.Get(), (inner ? 1.9f : 1.3f) * sc);
            }
            prev = pt;
            has_prev = true;
            head_depth = depth;
        }
        if (drawn < 1 && has_prev) { // The pen's bright head while it draws.
            radial(prev, 10.0f, {{0.0f, holo(0.8f * head_depth, 0.97f)}, {0.4f, holo(0.25f * head_depth, 0.8f)}, {1.0f, clear}});
        }
    }

    // 3. Telemetry comb sectors along the outer limb, ticking in one by one.
    for (int sec = 0; sec < 3; ++sec) {
        const float shown_ticks = easeOut(stage(0.3f + sec * 0.07f, 0.72f + sec * 0.07f)) * 28.0f;
        if (shown_ticks <= 0.01f) continue;
        const float tilt_x = 0.55f + sec * 1.05f;
        const float tilt_z = -0.35f + sec * 0.82f;
        const float ctx = std::cos(tilt_x), stx = std::sin(tilt_x);
        const float ctz = std::cos(tilt_z), stz = std::sin(tilt_z);
        const float start_a = sec * 2.1f + elapsed_ * (sec % 2 == 0 ? 0.05f : -0.04f);
        D2D1_POINT_2F prev_outer{};
        bool has_outer = false;
        for (int t = 0; t <= 28 && static_cast<float>(t) <= shown_ticks; ++t) {
            const float a = start_a + static_cast<float>(t) * 0.052f;
            const float r_in = 0.96f;
            const float tick_h = (t % 4 == 0 ? 0.095f : t % 2 == 0 ? 0.06f : 0.035f);
            const float r_out = r_in + tick_h;
            const float ca = std::cos(a), sa = std::sin(a);
            const auto rot3d = [&](float r, float& ox, float& oy, float& oz) {
                const float x0 = r * ca, z0 = r * sa;
                const float y1 = -z0 * stx, z1 = z0 * ctx;
                ox = x0 * ctz - y1 * stz;
                oy = x0 * stz + y1 * ctz;
                oz = z1;
            };
            float ix = 0, iy = 0, iz = 0, ox = 0, oy = 0, oz = 0;
            rot3d(r_in, ix, iy, iz);
            rot3d(r_out, ox, oy, oz);
            float six = 0, siy = 0, irz = 0, isc = 1;
            float sox = 0, soy = 0, orz = 0, osc = 1;
            project(ix, iy, iz, six, siy, irz, isc);
            project(ox, oy, oz, sox, soy, orz, osc);
            const float depth = std::clamp(1.05f - (orz + 1.0f) * 0.42f, 0.12f, 1.0f);
            brush(holo((t % 4 == 0 ? 0.4f : 0.22f) * depth, 0.6f));
            target_->DrawLine(D2D1::Point2F(six, siy), D2D1::Point2F(sox, soy), solid_.Get(), t % 4 == 0 ? 1.6f : 1.0f);
            if (t % 4 == 0) {
                float r2x = 0, r2y = 0, r2z = 0, s2x = 0, s2y = 0, r2rz = 0, s2sc = 1;
                rot3d(r_in + 0.095f, r2x, r2y, r2z);
                project(r2x, r2y, r2z, s2x, s2y, r2rz, s2sc);
                const D2D1_POINT_2F cur_outer = D2D1::Point2F(s2x, s2y);
                if (has_outer) {
                    brush(holo(0.26f * depth, 0.48f));
                    target_->DrawLine(prev_outer, cur_outer, solid_.Get(), 1.1f);
                }
                prev_outer = cur_outer;
                has_outer = true;
            }
        }
    }

    // 4. Organic particle continents (~1,150 embers): they fly out of the core to their shells,
    // the inner ones first, then breathe with a travelling wave of light.
    {
        constexpr int particle_count = 1150;
        D2D1_POINT_2F prev_pt{};
        float prev_rz = 0;
        bool prev_valid = false;
        for (int i = 0; i < particle_count; ++i) {
            const float fi = static_cast<float>(i);
            const float k = fi + 0.5f;
            const float phi = std::acos(1.0f - 2.0f * k / particle_count);
            const float theta = 2.3999632f * fi;
            const float ridge = std::sin(phi * 3.5f + std::cos(theta * 2.0f)) * std::cos(theta * 3.0f - phi * 2.0f);
            if (ridge < -0.22f && (i % 5 != 0)) { prev_valid = false; continue; }
            const float shell_base = i % 9 == 0 ? 0.42f : i % 4 == 0 ? 0.78f : 0.97f;
            const float start = 0.07f + 0.3f * (shell_base - 0.42f) / 0.55f + 0.24f * hash01(fi);
            const float out = easeOut(span(build, start, start + 0.3f));
            if (out <= 0.001f) { prev_valid = false; continue; }
            const float drift = 0.035f * std::sin(fi * 0.19f + elapsed_ * 0.85f);
            const float shell = (shell_base + drift + ridge * 0.04f) * (0.22f + 0.78f * out);
            const float sp = std::sin(phi), cp = std::cos(phi);
            const float px = shell * sp * std::cos(theta + ridge * 0.18f);
            const float py = shell * cp;
            const float pz = shell * sp * std::sin(theta + ridge * 0.18f);
            float sx = 0, sy = 0, rz = 0, sc = 1;
            project(px, py, pz, sx, sy, rz, sc);
            const float depth = std::clamp(1.08f - (rz + 1.0f) * 0.44f, 0.10f, 1.0f);
            const float shimmer = 0.45f + 0.55f * std::sin(elapsed_ * (1.4f + (i % 7) * 0.28f) + fi * 0.91f);
            const float wave_flare = std::max(0.0f, std::cos(phi * 2.8f - elapsed_ * 1.5f + std::sin(theta)));
            const float flare = wave_flare * wave_flare * wave_flare;
            const float heat = std::clamp(0.35f + ridge * 0.45f + flare * 0.42f + (i % 13 == 0 ? 0.25f : 0.0f), 0.15f, 1.0f);
            const float sz = (i % 17 == 0 ? 2.3f : i % 5 == 0 ? 1.35f : 0.85f) * sc * (0.82f + 0.36f * shimmer + 0.35f * flare);
            const float op = (i % 17 == 0 ? 0.66f : 0.34f) * depth * (0.45f + 0.55f * shimmer + 0.4f * flare) * std::min(1.0f, out * 1.5f);
            if (i % 17 == 0 || flare > 0.45f) {
                brush(holo(op * 0.2f, heat * 0.8f));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), sz * 3.6f, sz * 3.6f), solid_.Get());
                brush(holo(op * 0.3f, heat * 0.85f));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), sz * 2.0f, sz * 2.0f), solid_.Get());
            }
            brush(holo(op, heat));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), sz, sz), solid_.Get());
            if (prev_valid && (i % 3 == 0) && std::abs(rz - prev_rz) < 0.22f) {
                const float dx = sx - prev_pt.x, dy = sy - prev_pt.y;
                if (dx * dx + dy * dy < (radius * 0.14f) * (radius * 0.14f)) {
                    brush(holo(0.16f * depth * (0.5f + 0.5f * shimmer) * out, heat * 0.8f));
                    target_->DrawLine(prev_pt, D2D1::Point2F(sx, sy), solid_.Get(), 1.0f);
                }
            }
            prev_pt = D2D1::Point2F(sx, sy);
            prev_rz = rz;
            prev_valid = true;
        }
        // Sparks shooting out beyond the surface and fading into space, once the sphere stands.
        const float sparks = stage(0.72f, 1.0f);
        for (int j = 0; j < 28 && sparks > 0.001f; ++j) {
            const float fj = static_cast<float>(j);
            const float period = 2.8f + (j % 5) * 0.45f;
            const float phase = std::fmod(elapsed_ + fj * 0.67f, period) / period;
            if (phase > 0.65f) continue; // Rest between ejections.
            const float progress = phase / 0.65f;
            const float fade = std::sin(progress * 3.14159265f) * sparks;
            const float phi = std::acos(1.0f - 2.0f * ((j * 17 % 28) + 0.5f) / 28.0f);
            const float theta = 2.3999632f * fj * 3.0f + std::floor((elapsed_ + fj * 0.67f) / period) * 1.7f;
            const float r_head = 0.96f + progress * (0.28f + (j % 3) * 0.09f);
            const float r_tail = std::max(0.94f, r_head - 0.11f * (1.0f - progress * 0.5f));
            const float sp = std::sin(phi), cp = std::cos(phi);
            const float ux = sp * std::cos(theta), uy = cp, uz = sp * std::sin(theta);
            float hx = 0, hy = 0, hrz = 0, hsc = 1;
            float tx_pt = 0, ty_pt = 0, trz = 0, tsc = 1;
            project(ux * r_head, uy * r_head, uz * r_head, hx, hy, hrz, hsc);
            project(ux * r_tail, uy * r_tail, uz * r_tail, tx_pt, ty_pt, trz, tsc);
            const float depth = std::clamp(1.05f - (hrz + 1.0f) * 0.4f, 0.15f, 1.0f);
            brush(holo(0.34f * fade * depth, 0.78f));
            target_->DrawLine(D2D1::Point2F(tx_pt, ty_pt), D2D1::Point2F(hx, hy), solid_.Get(), 1.4f * hsc);
            brush(holo(0.22f * fade * depth, 0.85f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), 4.6f * hsc, 4.6f * hsc), solid_.Get());
            brush(holo(0.9f * fade * depth, 0.98f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), 1.6f * hsc, 1.6f * hsc), solid_.Get());
        }
    }

    // 5. Her memories as stars: each pops in shell by shell (the core first), a thread reaching it
    // from its parent just before.
    struct ProjectedNode {
        size_t index = 0;
        float sx = 0, sy = 0, rz = 0, scale = 1;
        float start = 0, appear = 0; // Its moment in the build, and how far it has come in (0–1).
    };
    const auto& nodes = view->mind_nodes;
    std::vector<ProjectedNode> projected(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto& p = projected[i];
        p.index = i;
        project(nodes[i].x, nodes[i].y, nodes[i].z, p.sx, p.sy, p.rz, p.scale);
        const float reach = std::sqrt(nodes[i].x * nodes[i].x + nodes[i].y * nodes[i].y + nodes[i].z * nodes[i].z);
        p.start = nodes[i].kind == Kind::core ? 0.04f : 0.3f + 0.34f * clamp01(reach) + 0.1f * fract(static_cast<float>(i) * 0.6180339f);
        p.appear = span(build, p.start, p.start + 0.22f);
    }
    const int selected_idx = nodes.empty() ? -1 : std::clamp(chat.mind_selected, 0, static_cast<int>(nodes.size()) - 1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind == Kind::core) continue;
        const size_t p = static_cast<size_t>(std::clamp(nodes[i].parent, 0, static_cast<int>(nodes.size()) - 1));
        if (p == i) continue;
        const auto& a = projected[p];
        const auto& b = projected[i];
        const float reach = easeInOut(span(build, b.start - 0.08f, b.start + 0.14f));
        if (reach <= 0.001f) continue;
        const bool matches = nodes[i].matches_filter && (nodes[p].matches_filter || nodes[p].kind == Kind::core);
        const bool active = matches && (static_cast<int>(i) == selected_idx || static_cast<int>(i) == chat.mind_hover || nodes[i].excited);
        const float depth = std::clamp(1.0f - (b.rz + 1.0f) * 0.36f, 0.18f, 1.0f);
        const float mx = (a.sx + b.sx) * 0.5f + (b.sy - cy) * 0.12f;
        const float my = (a.sy + b.sy) * 0.5f - (b.sx - cx) * 0.12f;
        // Only the part of the curve drawn so far (the quadratic split at `reach`).
        const float u = 1.0f - reach;
        const auto q1 = D2D1::Point2F(a.sx + (mx - a.sx) * reach, a.sy + (my - a.sy) * reach);
        const auto q2 = D2D1::Point2F(u * u * a.sx + 2.0f * u * reach * mx + reach * reach * b.sx, u * u * a.sy + 2.0f * u * reach * my + reach * reach * b.sy);
        ComPtr<ID2D1PathGeometry> arc;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(arc.GetAddressOf())) && SUCCEEDED(arc->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(D2D1::Point2F(a.sx, a.sy), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(q1, q2));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            brush(!matches ? gray(0.45f, 0.025f * depth * alpha) : active ? holo(0.12f, 0.85f) : holo(0.05f * depth, 0.6f));
            target_->DrawGeometry(arc.Get(), solid_.Get(), active ? 9.0f : 5.0f, companion_stroke_.Get());
            if (active) { brush(holo(0.24f, 0.9f)); target_->DrawGeometry(arc.Get(), solid_.Get(), 4.0f, companion_stroke_.Get()); }
            brush(!matches ? gray(0.52f, 0.12f * depth * alpha) : active ? holo(0.85f, 0.97f) : holo(0.3f * depth, 0.62f));
            target_->DrawGeometry(arc.Get(), solid_.Get(), active ? 2.0f : 1.35f, companion_stroke_.Get());
        }
        if (!matches) continue;
        if (reach < 1) { // The thread's bright tip as it reaches out.
            radial(q2, 8.0f, {{0.0f, holo(0.85f * depth, 0.98f)}, {0.45f, holo(0.25f * depth, 0.8f)}, {1.0f, clear}});
            continue;
        }
        // A pulse travelling along the thread.
        const float t = std::fmod(elapsed_ * (active ? 0.75f : 0.32f) + static_cast<float>(i) * 0.19f, 1.0f);
        const float w = 1.0f - t;
        const float ix = w * w * a.sx + 2.0f * w * t * mx + t * t * b.sx;
        const float iy = w * w * a.sy + 2.0f * w * t * my + t * t * b.sy;
        const float pulse_r = active ? 7.0f : 5.0f;
        radial(D2D1::Point2F(ix, iy), pulse_r, {{0.0f, holo((active ? 0.95f : 0.6f) * depth, 0.98f)}, {0.4f, holo((active ? 0.35f : 0.18f) * depth, 0.85f)}, {1.0f, clear}});
    }

    // 6. The stars, back to front: a wide soft glow (a radial gradient per heat), a body and a
    // white core; a masked thought is a veiled ring; the selection pulses with a ring and brackets.
    {
        const auto glow_brush = [&](float heat) {
            ComPtr<ID2D1RadialGradientBrush> fill;
            ComPtr<ID2D1GradientStopCollection> collection;
            const D2D1_GRADIENT_STOP stops[4]{{0.0f, holo(1.0f, heat)}, {0.2f, holo(0.5f, heat * 0.92f)}, {0.55f, holo(0.13f, heat * 0.78f)}, {1.0f, clear}};
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 4, collection.GetAddressOf())))
                target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({}, {}, 1, 1), collection.Get(), fill.GetAddressOf());
            return fill;
        };
        const std::array<ComPtr<ID2D1RadialGradientBrush>, 3> glows{glow_brush(0.66f), glow_brush(0.84f), glow_brush(0.97f)};
        auto sorted = projected;
        std::sort(sorted.begin(), sorted.end(), [](const ProjectedNode& a, const ProjectedNode& b) { return a.rz > b.rz; });
        for (const auto& item : sorted) {
            if (item.appear <= 0.001f) continue;
            const auto& node = nodes[item.index];
            const bool is_selected = static_cast<int>(item.index) == selected_idx;
            const bool is_hovered = static_cast<int>(item.index) == chat.mind_hover;
            const bool is_excited = node.excited;
            const bool is_core = node.kind == Kind::core;
            const float depth = std::clamp(1.06f - (item.rz + 1.0f) * 0.38f, 0.22f, 1.0f);
            const float pop = easeBack(item.appear), fade = std::min(1.0f, item.appear * 1.8f);
            const float node_pulse = 0.65f + 0.35f * std::sin(elapsed_ * 2.4f + static_cast<float>(item.index) * 1.13f);
            const float r = (is_core ? 6.5f : 3.3f * node.weight) * item.scale * pop *
                            (is_selected ? 1.35f : is_hovered || is_excited ? 1.22f : (0.92f + 0.18f * node_pulse));
            const int heat_level = is_core || is_selected || is_excited ? 2 : node.kind == Kind::note || node.kind == Kind::reflection || node.kind == Kind::thought ? 1 : 0;
            const float heat = heat_level == 2 ? 0.96f : heat_level == 1 ? 0.84f : 0.64f;
            const D2D1_POINT_2F at = D2D1::Point2F(item.sx, item.sy);
            if (!node.matches_filter) {
                brush(gray(0.58f, 0.26f * depth * fade * alpha));
                target_->FillEllipse(D2D1::Ellipse(at, r * 0.8f, r * 0.8f), solid_.Get());
                if (is_selected || is_hovered) target_->DrawEllipse(D2D1::Ellipse(at, r * 2, r * 2), solid_.Get(), 1);
                continue;
            }
            if (const auto& glow = glows[static_cast<size_t>(heat_level)]) {
                const float glow_r = r * (is_selected || is_excited ? 9.0f : is_core ? 8.0f : 6.5f) * (0.85f + 0.3f * node_pulse);
                glow->SetCenter(at);
                glow->SetRadiusX(glow_r);
                glow->SetRadiusY(glow_r);
                glow->SetOpacity(std::min(1.0f, (is_selected || is_excited ? 0.8f : 0.3f + 0.25f * node_pulse) * depth * fade));
                target_->FillEllipse(D2D1::Ellipse(at, glow_r, glow_r), glow.Get());
            }
            if (node.masked) {
                // Veiled: a ring and a dim heart instead of a bright core.
                brush(holo(0.85f * depth * fade, heat));
                target_->DrawEllipse(D2D1::Ellipse(at, r * 1.3f, r * 1.3f), solid_.Get(), 1.4f);
                brush(holo(0.5f * depth * fade, heat));
                target_->FillEllipse(D2D1::Ellipse(at, r * 0.42f, r * 0.42f), solid_.Get());
            } else {
                brush(holo((is_selected || is_excited ? 0.6f : 0.36f * node_pulse) * depth * fade, heat));
                target_->FillEllipse(D2D1::Ellipse(at, r * 1.6f, r * 1.6f), solid_.Get());
                brush(D2D1::ColorF(1.0f, 0.98f, 1.0f, (is_selected || is_excited ? 1.0f : (0.68f + 0.3f * node_pulse) * depth) * fade * alpha));
                target_->FillEllipse(D2D1::Ellipse(at, r * 0.72f, r * 0.72f), solid_.Get());
            }
            if (is_selected) { // A ring keeps rippling out of the chosen one.
                const float ripple = fract(elapsed_ * 0.6f);
                brush(holo(0.6f * (1 - ripple) * (1 - ripple) * fade, 0.95f));
                target_->DrawEllipse(D2D1::Ellipse(at, r * (2.2f + 5.0f * ripple), r * (2.2f + 5.0f * ripple)), solid_.Get(), 1.4f);
            }
            if (is_selected || is_hovered) {
                const float box_r = r * 2.5f + 4.0f;
                const float tick = 5.0f;
                brush(is_selected ? holo(0.95f * fade, 0.92f) : gray(1.0f, 0.65f * alpha * fade));
                for (const float dx : {-1.0f, 1.0f}) {
                    for (const float dy : {-1.0f, 1.0f}) {
                        const float px = item.sx + dx * box_r, py = item.sy + dy * box_r;
                        target_->DrawLine(D2D1::Point2F(px, py), D2D1::Point2F(px - dx * tick, py), solid_.Get(), 1.3f);
                        target_->DrawLine(D2D1::Point2F(px, py), D2D1::Point2F(px, py - dy * tick), solid_.Get(), 1.3f);
                    }
                }
            }
        }
    }

    if (context) context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    while (faded_layers-- > 0) target_->PopLayer();

    // The label beside the selected (or hovered) star, on a dark plate in her colour; a hidden
    // thought keeps its words masked there too.
    const float labels_in = stage(0.78f, 1.0f);
    if (labels_in > 0.003f) {
        auto* font = friend_status_font_.Get();
        const float saved = alpha;
        alpha *= labels_in;
        for (const auto& item : projected) {
            const bool is_sel = static_cast<int>(item.index) == selected_idx;
            const bool is_hov = static_cast<int>(item.index) == chat.mind_hover;
            if ((!is_sel && !is_hov) || item.appear < 0.99f || !inside(item.sx, item.sy)) continue;
            const auto& node = nodes[item.index];
            if (!node.matches_filter) continue;
            constexpr float inset = 12.0f, mask_w = 88.0f;
            const std::wstring text = node.masked ? node.category : readable(node.label);
            const float text_w = std::min(textAdvance(text, font) + 2.0f, 220.0f);
            const float tw = node.masked ? text_w + 10.0f + mask_w : text_w;
            const float dir = item.sx >= cx ? 1.0f : -1.0f;
            float lx = dir > 0 ? item.sx + 16.0f + inset : item.sx - 16.0f - inset - tw;
            lx = std::clamp(lx, sphere_rect.left + inset, std::max(sphere_rect.left + inset, sphere_rect.right - inset - tw));
            const float ly = item.sy - 12.0f;
            const float edge = lx + tw * 0.5f < item.sx ? lx + tw + inset : lx - inset;
            brush(is_sel ? charColor(0.6f) : gray(1.0f, 0.35f * alpha));
            target_->DrawLine(D2D1::Point2F(item.sx + (edge > item.sx ? 5.0f : -5.0f), item.sy), D2D1::Point2F(edge, ly), solid_.Get(), 1.0f);
            const auto plate = D2D1::RectF(lx - inset, ly - 14.0f, lx + tw + inset, ly + 14.0f);
            const auto shape = desktop_style_ ? D2D1::RoundedRect(plate, 7, 7) : pill(plate);
            brush(D2D1::ColorF(0.03f, 0.032f, 0.04f, 0.86f * alpha));
            target_->FillRoundedRectangle(shape, solid_.Get());
            brush(is_sel ? charColor(0.5f) : gray(1.0f, 0.22f * alpha));
            target_->DrawRoundedRectangle(shape, solid_.Get(), 1.0f);
            textLine(text, lx, ly, text_w, font, is_sel ? charColor(0.98f) : gray(0.88f, alpha));
            if (node.masked)
                drawMaskedText(readable(node.label), lx + text_w + 10.0f, ly - (typeMetrics(font).baseline - typeMetrics(font).cap * 0.5f), mask_w, font, 1,
                               charColor(1), alpha, seedOf(node.detail));
        }
        alpha = saved;
    }

    // What she is thinking right now, in a bar under the sphere (her hidden thought masked until
    // the bar is clicked); otherwise how to turn the sphere.
    const auto& tel = view->mind_telemetry;
    {
        const auto bar = box(layout.mindLiveBar());
        const float bcy = (bar.top + bar.bottom) * 0.5f;
        auto* font = friend_status_font_.Get();
        const float bar_in = stage(0.6f, 0.9f);
        if (!tel.live_thought.empty() && bar_in > 0.003f) {
            const float saved = alpha;
            alpha *= bar_in;
            const bool hovered = chat.mind_hover == companion_ui::mind_live_hit;
            const auto shape = desktop_style_ ? D2D1::RoundedRect(bar, 10, 10) : pill(bar);
            brush(D2D1::ColorF(0.05f, 0.054f, 0.065f, 0.86f * alpha));
            target_->FillRoundedRectangle(shape, solid_.Get());
            if (hovered) { brush(gray(1, 0.05f * alpha)); target_->FillRoundedRectangle(shape, solid_.Get()); }
            brush(charColor(hovered ? 0.8f : tel.live_busy ? 0.55f : 0.3f));
            target_->DrawRoundedRectangle(shape, solid_.Get(), 1.2f);
            const float dot_pulse = tel.live_busy ? (0.45f + 0.55f * std::sin(elapsed_ * 6.0f)) : 0.85f;
            brush(charColor(0.95f * dot_pulse));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bar.left + 18.0f, bcy), 3.8f, 3.8f), solid_.Get());
            const std::wstring prefix = tel.live_thought_kind.empty() ? std::wstring{} : tel.live_thought_kind + L"  ";
            const float prefix_w = textAdvance(prefix, font);
            textLine(prefix, bar.left + 32.0f, bcy, prefix_w + 4.0f, font, charColor(0.96f));
            const std::wstring action = tel.live_hidden ? std::wstring(tel.live_masked ? tr(L"Pokaż") : tr(L"Ukryj")) : std::wstring{};
            const float action_w = action.empty() ? 0.0f : textAdvance(action, font) + 16.0f;
            const float text_x = bar.left + 32.0f + prefix_w, text_w = bar.right - 20.0f - action_w - text_x;
            if (tel.live_masked)
                drawMaskedText(tel.live_thought, text_x, bcy - (typeMetrics(font).baseline - typeMetrics(font).cap * 0.5f), text_w, font, 1,
                               charColor(1), alpha * (hovered ? 1.25f : 1.0f), seedOf(tel.live_thought));
            else textLine(readable(tel.live_thought), text_x, bcy, text_w, font, gray(0.92f, alpha));
            if (!action.empty())
                textLine(action, bar.right - 18.0f - action_w, bcy, action_w, font, hovered ? charColor(1) : charColor(0.7f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            alpha = saved;
        } else if (tel.live_thought.empty()) {
            textLine(tr(L"Przeciągnij, by obrócić · kółkiem przybliżysz · strzałkami wybierzesz wspomnienie"), sphere_rect.left, bcy, sphere_rect.right - sphere_rect.left,
                     friend_status_font_.Get(), palette::muted(0.75f * alpha * stage(0.6f, 1.0f)), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // Right: her profile, four counters and the memory card — one column of glass cards a shared
    // gap apart, each coming up in turn as the sphere builds.
    constexpr float pad = MindLayout::mind_padding;
    const float card_radius = desktop_style_ ? 12.0f : 18.0f;
    // A card: flat — the frosted scene under an even tint (no sheen) and a faint hairline, barely
    // there (a little stronger with Ramki paneli on).
    const auto card = [&](D2D1_RECT_F r) {
        const auto shape = D2D1::RoundedRect(r, card_radius, card_radius);
        const auto glass = glassPanel(shape);
        if (glass.frosted) frostDraw(glass.shape.Get(), alpha);
        brush(D2D1::ColorF(0.064f, 0.068f, 0.08f, (glass.frosted ? 0.84f : 0.96f) * alpha));
        target_->FillRoundedRectangle(shape, solid_.Get());
        brush(gray(1.0f, (panel_borders_ ? 0.16f : 0.045f) * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 0.35f, r.top + 0.35f, r.right - 0.35f, r.bottom - 0.35f),
                                                        card_radius - 0.35f, card_radius - 0.35f), solid_.Get(), 0.7f);
        return shape;
    };
    // A card comes up from a little below and fades in, in its turn.
    const auto entering = [&](int order, const auto& draw) {
        const float in = easeOut(stage(0.1f + 0.08f * static_cast<float>(order), 0.48f + 0.08f * static_cast<float>(order)));
        if (in <= 0.003f) return;
        D2D1_MATRIX_3X2_F saved{};
        target_->GetTransform(&saved);
        const auto screen = D2D1::Matrix3x2F::ReinterpretBaseType(&saved)[0];
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, (1 - in) * 22.0f) * screen);
        alpha = base_alpha * in;
        draw();
        alpha = base_alpha;
        target_->SetTransform(saved);
    };
    // A small pill, filled only: her colour for what is hers (her persona, the memory's kind, what
    // she likes), else neutral. Returns its width, or 0 (and draws nothing) when it would pass `right`.
    const auto badge = [&](float x, float cy_badge, const std::wstring& label, bool accent, float right) -> float {
        auto* font = friend_status_font_.Get();
        const float w = textAdvance(label, font) + 24.0f;
        if (label.empty() || x + w > right) return 0.0f;
        const auto r = D2D1::RectF(x, cy_badge - 13.0f, x + w, cy_badge + 13.0f);
        const auto shape = desktop_style_ ? D2D1::RoundedRect(r, 7, 7) : pill(r);
        brush(accent ? D2D1::ColorF(tint.r, tint.g, tint.b, 0.18f * alpha) : gray(1.0f, 0.07f * alpha));
        target_->FillRoundedRectangle(shape, solid_.Get());
        brush(accent ? D2D1::ColorF(tint.r, tint.g, tint.b, 0.3f * alpha) : gray(1.0f, 0.1f * alpha));
        target_->DrawRoundedRectangle(shape, solid_.Get(), 1.0f);
        textLine(label, r.left, cy_badge, w, font, accent ? charColor(0.98f) : gray(0.84f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        return w;
    };

    // 1. Her profile: her figure standing on the card's bottom edge (cut by the card's shape) in her
    // light; beside her, her name with her persona (and the stage of their friendship with
    // gamification), her mood now, who she is and what she likes.
    entering(0, [&] {
        const auto profile = box(layout.mindProfile());
        const auto shape = card(profile);
        const float fig_h = (profile.bottom - profile.top) * 1.12f;
        const float fig_x = profile.left + 104.0f;
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(shape, clip.GetAddressOf()))) {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
            radial(D2D1::Point2F(fig_x, profile.bottom - 56.0f), 132.0f, {{0.0f, holo(0.3f, 0.7f)}, {0.5f, holo(0.1f, 0.5f)}, {1.0f, clear}});
            drawCompanionFigure(view->look, view->pose, fig_x, profile.bottom + fig_h * 0.08f, fig_h, alpha, view->costume);
            target_->PopLayer();
        }
        const float tx = profile.left + 212.0f, right = profile.right - pad, tw = right - tx;
        auto* name_font = category_font_.Get();
        auto* mood_font = friend_status_font_.Get();
        auto* body = settings_body_font_.Get();
        const auto name_metrics = typeMetrics(name_font), mood_metrics = typeMetrics(mood_font);
        const bool tastes = !tel.likes.empty() || !tel.dislikes.empty();
        const bool mood = !tel.mood.empty();
        const int bio_lines = tastes ? (mood ? 2 : 3) : (mood ? 3 : 4);
        constexpr float mood_gap = 8.0f, bio_gap = 8.0f, chip_gap = 14.0f, chip_height = 26.0f;
        const float bio_h = paragraph(tel.bio, tx, 0, tw, body, palette::muted(alpha), bio_lines, false);
        const float group_h = name_metrics.line - name_metrics.capTop() + (mood ? mood_gap + mood_metrics.line : 0.0f) +
                              (bio_h > 0 ? bio_gap + bio_h : 0.0f) + (tastes ? chip_gap + chip_height : 0.0f);
        const float line_top = std::max(profile.top + pad - 4, profile.top + (profile.bottom - profile.top - group_h) * 0.5f) - name_metrics.capTop();
        const float name_y = line_top + name_metrics.baseline - name_metrics.cap * 0.5f;
        textLine(view->name, tx, name_y, tw, name_font, palette::text(alpha));
        float bx = tx + std::min(textAdvance(view->name, name_font), tw) + 12.0f;
        badge(bx, name_y, view->persona, true, right);
        float below = line_top + name_metrics.line;
        if (mood) { // A dot in her colour and how she feels now.
            const float mood_y = below + mood_gap + mood_metrics.line * 0.5f;
            brush(charColor(0.9f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + 4.0f, mood_y), 3.2f, 3.2f), solid_.Get());
            textLine(tel.mood, tx + 14.0f, mood_y, tw - 14.0f, mood_font, charColor(0.92f));
            below += mood_gap + mood_metrics.line;
        }
        const float bio_top = below + bio_gap;
        paragraph(tel.bio, tx, bio_top, tw, body, palette::muted(alpha), bio_lines);
        float chip_x = tx;
        const float chip_y = bio_top + bio_h + chip_gap + chip_height * 0.5f;
        for (size_t i = 0; i < tel.likes.size() && i < 3; ++i) {
            const float w = badge(chip_x, chip_y, L"+ " + tel.likes[i], true, right);
            if (w <= 0) break;
            chip_x += w + 6.0f;
        }
        for (size_t i = 0; i < tel.dislikes.size() && i < 2; ++i) {
            const float w = badge(chip_x, chip_y, L"\u2212 " + tel.dislikes[i], false, right);
            if (w <= 0) break;
            chip_x += w + 6.0f;
        }
    });

    // 2. Four counters, each a number over its caption and a bar: her level and sympathy towards
    // the next one with gamification (else her unlocked outfits), then how full each part of her
    // memory is. They count up as the sphere builds.
    entering(1, [&] {
        const auto stats = box(layout.mindStats());
        card(stats);
        const float anim = easeOut(stage(0.25f, 0.85f));
        const auto counted = [&](int64_t value) { return std::to_wstring(std::llround(static_cast<double>(value) * anim)); };
        const auto filled = [](int value, size_t capacity) { return std::min(1.0f, static_cast<float>(value) / static_cast<float>(capacity)); };
        struct Stat { std::wstring value, caption; float ratio; D2D1_COLOR_F color; };
        const auto memory_ink = charColor(0.92f);
        const Stat first = view->affinity
            ? Stat{L"Lv " + std::to_wstring(std::max<int64_t>(1, std::llround(view->affinity_level * anim))),
                   counted(tel.points) + L" " + tr(L"pkt sympatii") + (view->streak >= 2 ? L" · \U0001F525" + std::to_wstring(view->streak) : L""),
                   view->affinity_progress, affinityPink(alpha)}
            : Stat{counted(tel.costumes_unlocked) + L" / " + std::to_wstring(tel.costumes_total), tr(L"Odblokowane stroje"),
                   tel.costumes_total > 0 ? static_cast<float>(tel.costumes_unlocked) / static_cast<float>(tel.costumes_total) : 1.0f, memory_ink};
        const std::array<Stat, 4> cells{{
            first,
            {counted(tel.notes_count), tr(L"Notatki o tobie"), filled(tel.notes_count, companion::max_notes_capacity), memory_ink},
            {counted(tel.summaries_count), tr(L"Wspomnienia rozmów"), filled(tel.summaries_count, companion::max_summaries_capacity), memory_ink},
            {counted(tel.picks_count), tr(L"Polecone gry"), filled(tel.picks_count, companion::max_picks_capacity), memory_ink},
        }};
        constexpr float cell_gap = 22.0f;
        const float cell_w = (stats.right - stats.left - 2 * pad - 3 * cell_gap) / 4.0f;
        for (size_t i = 0; i < cells.size(); ++i) {
            const float x0 = stats.left + pad + static_cast<float>(i) * (cell_w + cell_gap);
            textLine(cells[i].value, x0, stats.top + 33.0f, cell_w, category_font_.Get(), palette::text(alpha));
            textLine(cells[i].caption, x0, stats.top + 60.0f, cell_w, friend_status_font_.Get(), palette::muted(alpha));
            const float bar_y = stats.bottom - 19.0f;
            brush(gray(1.0f, 0.09f * alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0, bar_y, x0 + cell_w, bar_y + 4.0f), 2.0f, 2.0f), solid_.Get());
            const float ratio = clamp01(cells[i].ratio);
            // Anything at all shows as a sliver; a full bar stays full. A soft glow under the fill.
            const float fill_w = ratio > 0 ? std::min(cell_w, std::max(5.0f, ratio * anim * cell_w)) : 0.0f;
            if (fill_w > 0) {
                auto glow = cells[i].color;
                glow.a *= 0.22f;
                brush(glow);
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0 - 1, bar_y - 2, x0 + fill_w + 1, bar_y + 6.0f), 4.0f, 4.0f), solid_.Get());
                brush(cells[i].color);
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0, bar_y, x0 + fill_w, bar_y + 4.0f), 2.0f, 2.0f), solid_.Get());
            }
        }
    });

    // 3. The memory card: the selected memory (its kind, how she felt, when, Pokaż for a hidden
    // thought, Usuń) and its whole text, a hairline, then the list of all of them, which follows the
    // selection on the sphere.
    const auto memory = box(layout.mindMemory());
    const bool has_selection = selected_idx >= 0 && static_cast<size_t>(selected_idx) < nodes.size();
    entering(2, [&] {
        card(memory);
        if (!has_selection) return;
        const auto& node = nodes[static_cast<size_t>(selected_idx)];
        const float head_y = memory.top + MindLayout::mind_header_y;
        const auto del = box(layout.mindDelete());
        const auto reveal = box(layout.mindReveal());
        const bool action = node.deletable || node.locked; // Usuń, or Odkryj for her private matter.
        const float head_right = node.revealable ? reveal.left - 12.0f : action ? del.left - 12.0f : memory.right - pad;
        float meta_x = memory.left + pad;
        if (const float w = badge(meta_x, head_y, node.category.empty() ? node.label : node.category, true, head_right); w > 0) meta_x += w + 8.0f;
        if (!node.memory_tier.empty()) {
            if (const float w = badge(meta_x, head_y, node.memory_tier, false, head_right); w > 0) meta_x += w + 8.0f;
        }
        if (!node.feeling_label.empty() && !node.masked) { // How she felt is part of the secret too.
            if (const float w = badge(meta_x, head_y, node.feeling_label, false, head_right); w > 0) meta_x += w + 12.0f;
        }
        if (!node.when.empty() && node.when != node.category)
            textLine(node.when, meta_x + 2.0f, head_y, head_right - meta_x - 2.0f, toast_sub_font_.Get(), palette::muted(alpha));
        const auto quiet = [&](D2D1_RECT_F r, bool hovered, D2D1_COLOR_F fill) {
            const auto shape = desktop_style_ ? D2D1::RoundedRect(r, 8, 8) : pill(r);
            if (hovered) { brush(fill); target_->FillRoundedRectangle(shape, solid_.Get()); }
            else drawButtonGlass(r, alpha, false);
        };
        if (node.revealable) { // Pokaż / Ukryj, with an eye (struck through to mask it again).
            const bool hovered = chat.mind_hover == companion_ui::mind_reveal_hit;
            quiet(reveal, hovered, charColor(0.5f));
            if (!hovered) { brush(charColor(0.45f)); target_->DrawRoundedRectangle(desktop_style_ ? D2D1::RoundedRect(reveal, 8, 8) : pill(reveal), solid_.Get(), 1.0f); }
            const auto ink = hovered ? gray(1.0f, alpha) : charColor(1.0f);
            const auto label = node.masked ? std::wstring(tr(L"Pokaż")) : std::wstring(tr(L"Ukryj"));
            const float label_w = textAdvance(label, settings_body_font_.Get());
            const float eye_x = (reveal.left + reveal.right - label_w - 26.0f) * 0.5f + 8.0f, eye_y = (reveal.top + reveal.bottom) * 0.5f;
            ComPtr<ID2D1PathGeometry> eye;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(eye.GetAddressOf())) && SUCCEEDED(eye->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(eye_x - 8.0f, eye_y), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(eye_x, eye_y - 8.5f), D2D1::Point2F(eye_x + 8.0f, eye_y)));
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(eye_x, eye_y + 8.5f), D2D1::Point2F(eye_x - 8.0f, eye_y)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) { brush(ink); target_->DrawGeometry(eye.Get(), solid_.Get(), 1.5f, companion_stroke_.Get()); }
            }
            brush(ink);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(eye_x, eye_y), 2.3f, 2.3f), solid_.Get());
            if (!node.masked) target_->DrawLine(D2D1::Point2F(eye_x - 7.0f, eye_y + 6.0f), D2D1::Point2F(eye_x + 7.0f, eye_y - 6.0f), solid_.Get(), 1.6f, companion_stroke_.Get());
            textLine(label, eye_x + 14.0f, eye_y, label_w + 4.0f, settings_body_font_.Get(), ink);
        }
        if (node.locked) {
            const bool hovered = chat.mind_hover == companion_ui::mind_delete_hit;
            quiet(del, hovered, charColor(0.55f));
            textLine(tr(L"Odkryj"), del.left, (del.top + del.bottom) * 0.5f, del.right - del.left, settings_body_font_.Get(),
                     hovered ? gray(1.0f, alpha) : palette::text(alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        } else if (node.deletable) {
            const bool hovered = chat.mind_hover == companion_ui::mind_delete_hit;
            quiet(del, hovered, palette::dangerStrong(0.85f * alpha));
            textLine(tr(L"Usuń"), del.left, (del.top + del.bottom) * 0.5f, del.right - del.left, settings_body_font_.Get(),
                     hovered ? gray(1.0f, alpha) : palette::danger(alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        // Its whole text, a size up; a hidden thought masked (a click on it shows it, as Pokaż does).
        const auto detail = box(layout.mindDetail());
        if (node.masked) {
            const bool hovered = chat.mind_hover == companion_ui::mind_reveal_hit;
            drawMaskedText(node.detail, detail.left, memory.top + MindLayout::mind_detail_top, detail.right - detail.left, details_body_font_.Get(), 3,
                           charColor(1), alpha * (hovered ? 1.3f : 1.0f), seedOf(node.detail));
        } else {
            paragraph(readable(node.detail), detail.left, memory.top + MindLayout::mind_detail_top, detail.right - detail.left, details_body_font_.Get(),
                      palette::text(alpha), 3);
        }
        brush(gray(1.0f, 0.07f * alpha));
        target_->FillRectangle(D2D1::RectF(memory.left + pad, memory.top + MindLayout::mind_divider_y, memory.right - pad,
                                           memory.top + MindLayout::mind_divider_y + 1.0f), solid_.Get());

        // The list: its title with the selection's place, then a row per memory (a fill under the
        // pointer, and for the selected one a brighter fill with her colour's bar on its left).
        const int total = static_cast<int>(nodes.size());
        const float title_y = memory.top + MindLayout::mind_list_title_y;
        const auto matching = std::count_if(nodes.begin(), nodes.end(), [](const auto& n) { return n.matches_filter; });
        const auto title = chat.mind_filter == companion_ui::MindFilter::all ? std::wstring(tr(L"Wszystkie wspomnienia")) :
            std::wstring(tr(L"Pasuje: ")) + std::to_wstring(matching) + L" / " + std::to_wstring(nodes.size());
        capsLine(title, memory.left + pad, title_y, memory.right - memory.left - 2 * pad, palette::muted(alpha));
        capsLine(std::to_wstring(selected_idx + 1) + L" / " + std::to_wstring(total), memory.left + pad, title_y, memory.right - memory.left - 2 * pad,
                 palette::muted(alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
        const int first = layout.mindListFirst(selected_idx, total);
        for (int slot = 0; slot < layout.mindListRows() && first + slot < total; ++slot) {
            const int idx = first + slot;
            const auto& item = nodes[static_cast<size_t>(idx)];
            const auto row = box(layout.mindListRow(slot));
            const bool is_sel = idx == selected_idx;
            const bool is_hov = idx == chat.mind_hover;
            const auto row_shape = D2D1::RoundedRect(row, 9, 9);
            if (is_sel || is_hov) {
                brush(gray(1.0f, (is_sel ? 0.075f : 0.04f) * alpha));
                target_->FillRoundedRectangle(row_shape, solid_.Get());
            }
            if (is_sel) {
                brush(item.matches_filter ? charColor(0.95f) : gray(0.5f, 0.3f * alpha));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row.left + 3.0f, row.top + 8.0f, row.left + 6.0f, row.bottom - 8.0f), 1.5f, 1.5f), solid_.Get());
            }
            const float rcy = (row.top + row.bottom) * 0.5f;
            // The dot glows like its star: the core and notes about the player brightest; a hidden
            // thought still masked is a ring.
            const auto dot = item.matches_filter ? charColor(is_sel ? 0.98f : item.kind == Kind::core ? 0.9f : item.kind == Kind::note ? 0.75f : 0.5f) : gray(0.5f, 0.3f * alpha);
            const auto dot_at = D2D1::Point2F(row.left + 18.0f, rcy);
            brush(dot);
            if (item.masked) target_->DrawEllipse(D2D1::Ellipse(dot_at, 3.4f, 3.4f), solid_.Get(), 1.3f);
            else target_->FillEllipse(D2D1::Ellipse(dot_at, is_sel ? 3.4f : 2.6f, is_sel ? 3.4f : 2.6f), solid_.Get());
            const float text_left = row.left + 32.0f, text_right = row.right - 16.0f;
            auto* when_font = toast_sub_font_.Get();
            const std::wstring right_meta = item.memory_tier.empty() ? item.when
                                          : item.when.empty()        ? item.memory_tier
                                                                     : item.memory_tier + L" · " + item.when;
            const float when_w = right_meta.empty() ? 0.0f : textAdvance(right_meta, when_font) + 18.0f;
            auto* row_font = settings_body_font_.Get();
            const auto row_ink = !item.matches_filter ? gray(0.58f, 0.35f * alpha) : is_sel ? palette::text(alpha) : gray(0.78f, alpha);
            if (item.masked) {
                const float lead = textAdvance(item.category, row_font) + 10.0f;
                textLine(item.category, text_left, rcy, lead, row_font, row_ink);
                drawMaskedText(item.detail, text_left + lead, rcy - (typeMetrics(row_font).baseline - typeMetrics(row_font).cap * 0.5f),
                               text_right - text_left - when_w - lead, row_font, 1, item.matches_filter ? charColor(1) : gray(0.5f), alpha * (item.matches_filter ? 0.85f : 0.2f), seedOf(item.detail));
            } else textLine(readable(item.detail), text_left, rcy, text_right - text_left - when_w, row_font, row_ink);
            if (!right_meta.empty())
                textLine(right_meta, text_left, rcy, text_right - text_left, when_font, palette::muted(alpha * (item.matches_filter ? 1.0f : 0.3f)), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    });

    // A hairline from the selected star to its card (drawn last, outside the card's motion).
    if (has_selection && labels_in > 0.003f && projected[static_cast<size_t>(selected_idx)].appear >= 0.99f &&
        inside(projected[static_cast<size_t>(selected_idx)].sx, projected[static_cast<size_t>(selected_idx)].sy)) {
        const auto& proj = projected[static_cast<size_t>(selected_idx)];
        const float head_y = memory.top + MindLayout::mind_header_y;
        const float elbow_x = sphere_rect.right + 2.0f;
        const float saved = alpha;
        alpha *= labels_in;
        brush(charColor(0.4f));
        target_->DrawLine(D2D1::Point2F(proj.sx, proj.sy), D2D1::Point2F(elbow_x, head_y), solid_.Get(), 1.0f);
        target_->DrawLine(D2D1::Point2F(elbow_x, head_y), D2D1::Point2F(memory.left, head_y), solid_.Get(), 1.0f);
        brush(charColor(0.95f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(memory.left, head_y), 3.0f, 3.0f), solid_.Get());
        alpha = saved;
    }

    // Usuń asks first, in the reset dialog's form, centred on the screen; the memory quoted (a hidden
    // thought still masked is not given away by it).
    if (has_selection && chat.mind_confirm) {
        const auto& node = nodes[static_cast<size_t>(selected_idx)];
        if (node.locked) { // Her private matter: the player looks, she never learns he did.
            drawCompanionQuestion(layout.mindConfirm(), tr(L"Zajrzeć do jej prywatnej sprawy?"),
                                  tr(L"Nie mówiła ci o tym. Ona się nie dowie, że zajrzałeś — ale jeśli o to zapytasz, może się zdziwić, skąd wiesz."),
                                  tr(L"Odkryj"), chat.mind_confirm_focus, chat.mind_hover, alpha);
        } else {
            const bool english = i18n::english();
            const auto body = node.masked ? std::wstring(tr(L"Ta ukryta myśl zniknie, zanim ją poznasz."))
                                          : (english ? L"\u201C" : L"\u201E") + readable(node.detail) + L"\u201D";
            drawCompanionQuestion(layout.mindConfirm(), tr(L"Usunąć to wspomnienie?"), body, tr(L"Usuń"), chat.mind_confirm_focus, chat.mind_hover, alpha);
        }
    }
}
