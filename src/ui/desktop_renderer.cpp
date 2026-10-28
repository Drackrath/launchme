// Desktop mode's library (src/ui/desktop_view.hpp): the top bar, the sidebar with her card, the
// poster grid and the selected game's pane, then the popups (menus, Pobierania, the dragged
// posters, tooltips). Dark neutral surfaces with hairline borders and the interface accent, the
// same artwork, tile shapes and typeface as Tryb TV. Popups: desktop_popups_renderer.cpp.
#include "renderer.hpp"
#include "brand_icons.hpp"
#include "renderer_internal.hpp"

#include <d2d1_2helper.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;

namespace {
using palette::gray;
D2D1_RECT_F box(Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
float smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
Rect inflate(Rect r, float by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }
// Surfaces: near-black with the TV mode's cool tint, a step lighter for each layer.
const D2D1_COLOR_F app_bg = rgb(0.043f, 0.047f, 0.055f);
const D2D1_COLOR_F side_bg = rgb(0.056f, 0.061f, 0.071f);
const D2D1_COLOR_F card_bg = rgb(0.086f, 0.092f, 0.106f);
std::wstring upper(std::wstring value) {
    if (!value.empty()) CharUpperBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}
}

// ---- Helpers ---------------------------------------------------------------------------------

void Renderer::desktopGlyph(wchar_t glyph, float cx, float cy, float size, D2D1_COLOR_F ink, float angle) {
    if (!desktop_icon_font_ || !glyph || ink.a <= 0.003f) return;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const float k = size / 16.0f;
    auto transform = D2D1::Matrix3x2F::Scale(k, k, D2D1::Point2F(cx, cy));
    if (std::abs(angle) > 0.01f) transform = transform * D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(cx, cy));
    target_->SetTransform(transform * base);
    brush(ink);
    // One cached layout per glyph (the icon font is centred both ways): the same pixels as DrawText.
    if (auto* layout = text_layouts_.get(dwrite_.Get(), std::wstring(1, glyph), desktop_icon_font_.Get(), 32, 32, text_variant_glyph))
        target_->DrawTextLayout(D2D1::Point2F(cx - 16, cy - 16), layout, solid_.Get());
    else {
        const wchar_t text[2] = {glyph, 0};
        target_->DrawText(text, 1, desktop_icon_font_.Get(), D2D1::RectF(cx - 16, cy - 16, cx + 16, cy + 16), solid_.Get());
    }
    target_->SetTransform(base);
}

void Renderer::desktopFill(Rect r, float radius, D2D1_COLOR_F ink) {
    if (ink.a <= 0.002f || r.right <= r.left || r.bottom <= r.top) return;
    brush(ink);
    target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
}

void Renderer::desktopStroke(Rect r, float radius, D2D1_COLOR_F ink, float width) {
    if (ink.a <= 0.002f || r.right <= r.left || r.bottom <= r.top) return;
    brush(ink);
    const float half = width * 0.5f;
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + half, r.top + half, r.right - half, r.bottom - half), radius, radius), solid_.Get(), width);
}

void Renderer::desktopShadow(Rect r, float radius, float strength) {
    if (strength <= 0.003f) return;
    for (int layer = 4; layer >= 1; --layer) {
        const float spread = static_cast<float>(layer) * 3.5f;
        desktopFill({r.left - spread, r.top - spread + 6, r.right + spread, r.bottom + spread + 8}, radius + spread, gray(0, 0.07f * strength));
    }
}

// A vertical two-stop gradient from y0 to y1, created once per device (its colours are fixed per
// slot) and moved each frame, rather than a new stop collection and brush on every paint.
ID2D1LinearGradientBrush* Renderer::desktopGradient(ComPtr<ID2D1LinearGradientBrush>& cached, D2D1_COLOR_F from, D2D1_COLOR_F to, float y0, float y1) {
    if (!cached) {
        ComPtr<ID2D1GradientStopCollection> stops;
        const D2D1_GRADIENT_STOP gradient_stops[] = {{0, from}, {1, to}};
        if (FAILED(target_->CreateGradientStopCollection(gradient_stops, 2, stops.GetAddressOf())) ||
            FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, y0), D2D1::Point2F(0, y1)), stops.Get(),
                                                      cached.GetAddressOf()))) return nullptr;
    }
    cached->SetStartPoint(D2D1::Point2F(0, y0));
    cached->SetEndPoint(D2D1::Point2F(0, y1));
    return cached.Get();
}

// The tile shape at the origin for a width × height poster (squircle_corner_/corner_reference_ as
// the caller set them): its fill and outline tessellated once for the current scale, and its soft
// shadow as a bitmap. Without ID2D1DeviceContext1 the fill and outline use the geometry.
const Renderer::PosterShape* Renderer::posterShape(float width, float height) {
    if (width <= 0 || height <= 0) return nullptr;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const float scale = std::sqrt(base._11 * base._11 + base._12 * base._12);
    const auto key = (static_cast<uint64_t>(std::lround(width * 16)) & 0xFFFFF) << 44 | (static_cast<uint64_t>(std::lround(height * 16)) & 0xFFFFF) << 24 |
                     (static_cast<uint64_t>(std::lround(scale * 100)) & 0x3FFFFF) << 2 | static_cast<uint64_t>(tile_shape_);
    if (auto found = poster_shapes_.find(key); found != poster_shapes_.end()) { found->second.used = ++poster_shape_clock_; return &found->second; }
    if (poster_shapes_.size() >= 24) // Sizes pass while the slider or the window changes: drop the stalest.
        poster_shapes_.erase(std::min_element(poster_shapes_.begin(), poster_shapes_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; }));
    PosterShape shape;
    shape.geometry = tileShape(D2D1::RectF(0, 0, width, height), tile_shape_);
    if (!shape.geometry) return nullptr;
    if (realizer_) {
        const float tolerance = D2D1::ComputeFlatteningTolerance(base);
        realizer_->CreateFilledGeometryRealization(shape.geometry.Get(), tolerance, shape.fill.GetAddressOf());
        realizer_->CreateStrokedGeometryRealization(shape.geometry.Get(), tolerance, 1, nullptr, shape.outline.GetAddressOf());
    }
    // desktopShadow's four layers drawn once into a bitmap at device pixels: one DrawBitmap per
    // poster instead of four large blended fills (about 1 ms per 4K frame for a dozen posters).
    // Drawn at strength s it is s × the full-strength layers (within a few percent of stacking
    // four layers of 0.07 × s each).
    constexpr float reach = 14, drop = 6;
    shape.shadow_box = D2D1::RectF(-reach, -reach + drop, width + reach, height + reach + drop + 2);
    const UINT pixel_width = static_cast<UINT>(std::ceil((shape.shadow_box.right - shape.shadow_box.left) * scale));
    const UINT pixel_height = static_cast<UINT>(std::ceil((shape.shadow_box.bottom - shape.shadow_box.top) * scale));
    ComPtr<ID2D1BitmapRenderTarget> layers;
    ComPtr<ID2D1SolidColorBrush> ink;
    if (scale > 0 && pixel_width && pixel_height && pixel_width <= 4096 && pixel_height <= 4096 &&
        SUCCEEDED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(pixel_width), static_cast<float>(pixel_height)),
                                                        D2D1::SizeU(pixel_width, pixel_height), layers.GetAddressOf())) &&
        SUCCEEDED(layers->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.07f), ink.GetAddressOf()))) {
        layers->BeginDraw();
        layers->Clear(D2D1::ColorF(0, 0, 0, 0));
        layers->SetTransform(D2D1::Matrix3x2F::Translation(-shape.shadow_box.left, -shape.shadow_box.top) * D2D1::Matrix3x2F::Scale(scale, scale));
        for (int layer = 4; layer >= 1; --layer) {
            const float spread = static_cast<float>(layer) * 3.5f, radius = 12 + spread;
            layers->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(-spread, -spread + 6, width + spread, height + spread + 8), radius, radius), ink.Get());
        }
        if (SUCCEEDED(layers->EndDraw())) layers->GetBitmap(shape.shadow.GetAddressOf());
    }
    shape.used = ++poster_shape_clock_;
    return &poster_shapes_.emplace(key, std::move(shape)).first->second;
}

void Renderer::desktopFocusRing(Rect r, float radius) {
    desktopStroke(inflate(r, 3), radius + 3, uiAccent(0.95f), 2);
}

void Renderer::desktopArc(float cx, float cy, float radius, float start, float sweep, D2D1_COLOR_F ink, float width) {
    if (std::abs(sweep) < 0.01f) return;
    sweep = std::clamp(sweep, -6.27f, 6.27f);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return;
    const auto point = [&](float angle) { return D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius); };
    sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(point(start + sweep), D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  sweep > std::numbers::pi_v<float> ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    brush(ink);
    target_->DrawGeometry(path.Get(), solid_.Get(), width);
}

float Renderer::desktopCenteredText(const std::wstring& value, float cx, float top, float width, IDWriteTextFormat* format,
                                    D2D1_COLOR_F ink, int max_lines) {
    if (value.empty() || width <= 0 || !format) return 0;
    const auto metrics = typeMetrics(format);
    const auto adjust = [](IDWriteTextLayout* layout) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    };
    // Cached: a placeholder title is drawn on every frame until the poster arrives.
    ComPtr<IDWriteTextLayout> created;
    auto* layout = text_layouts_.get(dwrite_.Get(), value, format, width, metrics.line * max_lines + 0.5f, text_variant_centered, adjust);
    if (!layout) {
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, width, metrics.line * max_lines + 0.5f,
                                             created.GetAddressOf()))) return 0;
        adjust(created.Get()); layout = created.Get();
    }
    DWRITE_TEXT_METRICS measured{};
    layout->GetMetrics(&measured);
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(cx - width * 0.5f, top), layout, solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return std::min(static_cast<float>(std::max(1u, measured.lineCount)), static_cast<float>(max_lines)) * metrics.line;
}

// A picture cropped to fill `box` around its centre.
void Renderer::desktopBitmapFill(ID2D1Bitmap* bitmap, D2D1_RECT_F target, float opacity) {
    if (!bitmap || opacity <= 0.003f) return;
    const auto size = bitmap->GetSize();
    const float w = target.right - target.left, h = target.bottom - target.top;
    if (size.width < 1 || size.height < 1 || w < 1 || h < 1) return;
    const float zoom = std::max(w / size.width, h / size.height);
    const float sw = w / zoom, sh = h / zoom;
    const auto source = D2D1::RectF((size.width - sw) * 0.5f, (size.height - sh) * 0.3f, (size.width + sw) * 0.5f, (size.height - sh) * 0.3f + sh);
    target_->DrawBitmap(bitmap, target, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
}

// style 0: ghost (no fill until hovered), 1: outline, 2: primary (accent fill, dark or light ink).
void Renderer::desktopButton(Rect r, int style, float hover, bool pressed, bool enabled, wchar_t glyph, const std::wstring& label) {
    const auto accent = uiAccent();
    const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
    const float radius = 8;
    D2D1_COLOR_F ink = enabled ? palette::text() : gray(0.5f);
    if (style == 2 && enabled) {
        desktopFill(r, radius, uiAccent(pressed ? 0.82f : 1.0f));
        if (hover > 0.01f) desktopFill(r, radius, luminance > 0.6f ? gray(0, 0.08f * hover) : gray(1, 0.10f * hover));
        ink = luminance > 0.6f ? gray(0.06f) : gray(0.98f);
    } else {
        if (style == 1 || !enabled) desktopFill(r, radius, gray(1, enabled ? 0.045f : 0.025f));
        if (enabled && hover > 0.01f) desktopFill(r, radius, gray(1, (pressed ? 0.11f : 0.065f) * hover));
        if (style == 1) desktopStroke(r, radius, gray(1, 0.085f + 0.05f * hover));
    }
    const float cy = (r.top + r.bottom) * 0.5f;
    const float label_width = label.empty() ? 0 : std::min(textAdvance(label, desktop_button_font_.Get()), r.right - r.left - (glyph ? 48 : 20));
    const float glyph_width = glyph ? 16.0f : 0.0f, gap = glyph && !label.empty() ? 9.0f : 0.0f;
    const float start = (r.left + r.right - (glyph_width + gap + label_width)) * 0.5f;
    if (glyph) desktopGlyph(glyph, start + 8, cy, 15, ink);
    if (!label.empty()) textLine(label, start + glyph_width + gap, cy, label_width + 2, desktop_button_font_.Get(), ink);
}

void Renderer::desktopInstallButton(Rect r, float fraction, const std::wstring& label) {
    desktopFill(r, 8, gray(1, 0.08f));
    const float span = r.right - r.left;
    if (fraction >= 0) desktopFill({r.left, r.top, r.left + span * std::clamp(fraction, 0.0f, 1.0f), r.bottom}, 8, uiAccent(0.32f));
    else {
        const float strip = span * 0.3f, phase = std::fmod(elapsed_ * 0.6f, 1.0f);
        const float x0 = r.left + (span + strip) * phase - strip;
        desktopFill({std::max(r.left, x0), r.top, std::min(r.right, x0 + strip), r.bottom}, 8, uiAccent(0.28f));
    }
    desktopStroke(r, 8, uiAccent(0.6f));
    textLine(label, r.left, (r.top + r.bottom) * 0.5f, span, desktop_button_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---- The frame -------------------------------------------------------------------------------

void Renderer::drawDesktop() {
    const auto& view = *context_->desktop;
    Layout layout;
    layout.width = viewport_.width; layout.height = viewport_.height;
    layout.poster_width = view.poster;
    layout.tools = static_cast<int>(view.tools.size());
    layout.companion_card = view.companion_card;
    layout.narrow = view.sidebar_narrow; layout.pane_width = view.pane_width;
    desktop_chips_.clear(); desktop_chip_add_ = {}; desktop_rating_ = {}; desktop_achievements_ = {}; desktop_cloud_ = {}; desktop_studios_ = {};
    desktop_hotspots_.clear(); // Widżety record their controls here.
    companion_bubble_ = {};
    brush(app_bg);
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    // The selected game's colours as a faint glow behind the grid, as the TV mode's backgrounds.
    if (selected_ < heroes_.size() && heroes_[selected_].bitmap)
        if (auto* tint = blurredArt(heroes_[selected_])) {
            const auto m = layout.main();
            target_->DrawBitmap(tint, D2D1::RectF(m.left - 60, m.top - 60, m.right + 60, m.top + (m.bottom - m.top) * 0.7f), 0.11f,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (auto* fade = desktopGradient(desktop_hero_fade_, rgb(app_bg.r, app_bg.g, app_bg.b, 0), app_bg, m.top + 120, m.top + (m.bottom - m.top) * 0.7f))
                target_->FillRectangle(D2D1::RectF(m.left, m.top, m.right, m.bottom), fade);
        }
    // No screen transition here: back from a dialog or page, the library is simply there again (the
    // TV mode's zoom and bow would distort the whole window).
    drawDesktopGrid(layout, view);
    drawDesktopPane(layout, view);
    drawDesktopSidebar(layout, view);
    drawDesktopTitlebar(layout, view);
    if (view.downloads_open) drawDesktopDownloads(layout, view);
    if (view.widgets_open) drawDesktopWidgets(layout, view);
    if (view.menu) drawDesktopMenu(layout, *view.menu);
    if (view.dragging) drawDesktopDrag(layout, view);
    drawDesktopTooltip(layout, view);
    drawDesktopQuick(view); // Ctrl+K over everything.
    drawDesktopMeta(view.meta);  // Edytuj metadane.
    // A restored window: a hairline along its top edge, where its frame no longer draws one.
    if (!view.maximized) { brush(gray(1, 0.07f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, 1.0f / viewport_.scale), solid_.Get()); }
}

// ---- Top bar ---------------------------------------------------------------------------------

void Renderer::drawDesktopTitlebar(const Layout& layout, const View& view) {
    const float ink_alpha = view.active_window ? 1.0f : 0.55f;
    brush(side_bg);
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, title_h), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(0, title_h - 1, viewport_.width, title_h), solid_.Get());
    // The sidebar's toggle (a panel with its left column), then the logo: symbol and wordmark while
    // the sidebar is wide, the symbol alone when it is narrow, crossfading with its animation.
    const float cy = title_h * 0.5f;
    {
        const auto toggle = layout.sidebarToggle();
        const bool hover = view.hover.target == Target::sidebar_toggle, pressed = view.pressed.target == Target::sidebar_toggle;
        if (hover || pressed) desktopFill(toggle, 8, gray(1, pressed ? 0.11f : 0.075f));
        const float cx = (toggle.left + toggle.right) * 0.5f, w = 16, h = 13;
        const auto frame = D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
        const auto ink = hover ? palette::text(ink_alpha) : palette::muted(ink_alpha);
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(frame, 2.5f, 2.5f), solid_.Get(), 1.4f);
        const float column = 6.0f - 2.5f * view.sidebar_narrow; // Narrower with the sidebar.
        target_->FillRectangle(D2D1::RectF(frame.left + 2.5f, frame.top + 2.5f, frame.left + 2.5f + column, frame.bottom - 2.5f), solid_.Get());
        if (view.keyboard && view.hover.target == Target::sidebar_toggle) desktopFocusRing(toggle, 8);
    }
    const float narrow = std::clamp(view.sidebar_narrow, 0.0f, 1.0f);
    if (narrow < 1) drawLaunchmeLogo(layout.logoLeft(), cy, desktop_ui::logo_height, palette::text(ink_alpha * (1 - narrow)));
    if (narrow > 0) drawLaunchmeLogo(layout.logoLeft(), cy, desktop_ui::logo_height, palette::text(ink_alpha * narrow), false, true);
    // Search.
    const auto search = layout.search();
    const bool search_hover = view.hover.target == Target::search || view.hover.target == Target::search_clear;
    desktopFill(search, 9, gray(1, view.search_focus ? 0.075f : search_hover ? 0.06f : 0.045f));
    if (view.search_focus) desktopStroke(inflate(search, 1), 10, uiAccent(0.9f), 1.5f);
    else desktopStroke(search, 9, gray(1, search_hover ? 0.14f : 0.08f));
    desktopGlyph(glyph::search, search.left + 18, cy, 14, palette::muted(ink_alpha));
    const float text_left = search.left + 36, text_right = search.right - (view.query.empty() ? 70 : 36);
    const auto metrics = typeMetrics(desktop_font_.Get());
    const float text_top = cy - metrics.line * 0.5f;
    if (view.query.empty()) {
        textLine(tr(L"Szukaj w bibliotece"), text_left, cy, text_right - text_left, desktop_font_.Get(), palette::muted(0.85f * ink_alpha));
        if (view.search_focus && view.caret_on) text(L"│", text_left - 1, text_top, 8, metrics.line, desktop_font_.Get(), palette::text());
        else if (!view.search_focus) {
            const Rect key{search.right - 62, cy - 10, search.right - 10, cy + 10};
            desktopStroke(key, 5, gray(1, 0.12f));
            // Ctrl+K: the quick search over everything (Ctrl+F still types here).
            textLine(L"Ctrl+K", key.left, cy, key.right - key.left, desktop_small_font_.Get(), palette::muted(ink_alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    } else {
        if (view.search_focus) desktopSelection(view.query, text_left, cy, text_right - text_left, desktop_font_.Get());
        fieldText(view.query, view.caret, view.search_focus && view.caret_on, text_left, text_top, text_right - text_left, metrics.line,
                  desktop_font_.Get(), palette::text(ink_alpha));
        const auto clear = layout.searchClear();
        if (view.hover.target == Target::search_clear) desktopFill(clear, 6, gray(1, 0.08f));
        desktopGlyph(glyph::close, (clear.left + clear.right) * 0.5f, cy, 10, palette::muted(ink_alpha));
    }
    // Shortcuts.
    for (int i = 0; i < static_cast<int>(view.tools.size()); ++i) {
        const auto r = layout.tool(i);
        const auto tool = view.tools[static_cast<size_t>(i)];
        const bool hover = view.hover == Hit{Target::tool, i};
        const bool pressed = view.pressed == Hit{Target::tool, i};
        const bool open = tool == Tool::downloads && view.downloads_open;
        if (hover || open) desktopFill(r, 8, gray(1, pressed ? 0.12f : open ? 0.1f : 0.07f));
        const float x = (r.left + r.right) * 0.5f, y = (r.top + r.bottom) * 0.5f;
        const auto ink = gray(hover || open ? 0.98f : 0.80f, ink_alpha);
        switch (tool) {
        case Tool::glance: drawGlanceGlyph(x, y, 15, ink); break;
        case Tool::tarot: drawTarotGlyph(x, y, 16, ink); break;
        case Tool::companion: desktopGlyph(glyph::people, x, y, 16, ink); break;
        case Tool::store: desktopGlyph(glyph::shop, x, y, 16, ink); break;
        case Tool::friends: { // Widżety: three tiles and a turned one, as a widgets icon.
            brush(ink);
            const float s = 6.4f, g = 2.8f, l = x - s - g * 0.5f, t = y - s - g * 0.5f;
            for (const auto& [tx, ty] : {std::pair{l, t}, std::pair{l, t + s + g}, std::pair{l + s + g, t + s + g}})
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + s, ty + s), 1.6f, 1.6f), solid_.Get(), 1.4f);
            const float dx = l + s + g + s * 0.5f, dy = t + s * 0.5f, d = s * 0.62f;
            ComPtr<ID2D1PathGeometry> diamond; ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(diamond.GetAddressOf())) && SUCCEEDED(diamond->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(dx, dy - d), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(dx + d, dy)); sink->AddLine(D2D1::Point2F(dx, dy + d)); sink->AddLine(D2D1::Point2F(dx - d, dy));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) target_->DrawGeometry(diamond.Get(), solid_.Get(), 1.4f);
            }
            break;
        }
        case Tool::tv: desktopGlyph(glyph::tv, x, y, 17, ink); break;
        case Tool::settings: desktopGlyph(glyph::settings, x, y, 16, ink); break;
        case Tool::update: {
            // A newer Launchme: an accent arrow with a dot; downloading, its ring fills.
            desktopGlyph(glyph::download, x, y, 15, uiAccent(ink_alpha));
            if (view.update_progress >= 0) {
                desktopArc(x, y, 13, 0, 2 * std::numbers::pi_v<float>, gray(1, 0.1f), 2);
                desktopArc(x, y, 13, -std::numbers::pi_v<float> * 0.5f, std::max(0.08f, view.update_progress) * 2 * std::numbers::pi_v<float>, uiAccent(ink_alpha), 2);
            } else {
                brush(uiAccent(ink_alpha));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.right - 8, r.top + 8), 4, 4), solid_.Get());
            }
            break;
        }
        case Tool::downloads: {
            desktopGlyph(glyph::download, x, y, 15, view.downloads_moving ? uiAccent(ink_alpha) : ink);
            if (view.downloads_moving) {
                // Its ring: the transfers' share, or a turning segment while none is known.
                desktopArc(x, y, 13, 0, 2 * std::numbers::pi_v<float>, gray(1, 0.1f), 2);
                if (view.downloads_fraction >= 0)
                    desktopArc(x, y, 13, -std::numbers::pi_v<float> * 0.5f, std::max(0.12f, view.downloads_fraction) * 2 * std::numbers::pi_v<float>, uiAccent(ink_alpha), 2);
                else desktopArc(x, y, 13, elapsed_ * 4.0f, 1.4f, uiAccent(ink_alpha), 2);
                const auto count = std::to_wstring(view.downloads_moving);
                const Rect badge{r.right - 15, r.top + 1, r.right - 1, r.top + 15};
                desktopFill(badge, 7, uiAccent(ink_alpha));
                const auto accent = uiAccent();
                const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
                textLine(count, badge.left, (badge.top + badge.bottom) * 0.5f, badge.right - badge.left, desktop_label_font_.Get(),
                         luminance > 0.6f ? gray(0.06f) : gray(0.98f), DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            break;
        }
        }
    }
    // Window buttons.
    for (int i = 0; i < 3; ++i) {
        const auto r = layout.windowButton(i);
        const Target target = i == 0 ? Target::minimize : i == 1 ? Target::maximize : Target::close;
        const bool hover = view.hover.target == target || (i == 1 && view.max_hover);
        if (hover) desktopFill(r, 0, i == 2 ? rgb(0.77f, 0.17f, 0.11f) : gray(1, 0.08f));
        const wchar_t g = i == 0 ? glyph::minimize : i == 1 ? (view.maximized ? glyph::restore : glyph::maximize) : glyph::window_close;
        desktopGlyph(g, (r.left + r.right) * 0.5f, cy, 10, gray(hover ? 1.0f : 0.82f, hover ? 1.0f : ink_alpha));
    }
}

// ---- Sidebar ---------------------------------------------------------------------------------

void Renderer::drawDesktopSidebar(const Layout& layout, const View& view) {
    const auto side = layout.sidebar();
    brush(side_bg);
    target_->FillRectangle(box(side), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(side.right - 1, side.top, side.right, side.bottom), solid_.Get());
    const float top = layout.sidebarTop(), bottom = layout.sidebarBottom();
    target_->PushAxisAlignedClip(D2D1::RectF(0, top, side.right - 1, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto* entries = view.entries;
    constexpr const wchar_t* groups[] = {trKey(L"Biblioteka"), trKey(L"Platformy"), trKey(L"Emulacja"), trKey(L"Kolekcje"), trKey(L"Wydawcy"),
                                         trKey(L"Developerzy")};
    // The active section's pill and left bar glide between entries on a spring.
    if (view.active_y) {
        bool drop_on_active = false;
        for (int i = 0; i < static_cast<int>(view.rows.size()); ++i)
            if (!view.rows[static_cast<size_t>(i)].heading && view.rows[static_cast<size_t>(i)].entry == view.active && view.drop_row == i)
                drop_on_active = true;
        if (!drop_on_active) {
            const float ay = layout.sidebarTop() + *view.active_y - view.sidebar_scroll;
            const Rect active_rect{10, ay, layout.sidebarRight() - 10, ay + row_h};
            if (active_rect.bottom >= top && active_rect.top <= bottom) {
                desktopFill(active_rect, 8, gray(1, 0.085f));
                desktopFill({active_rect.left, active_rect.top + 8, active_rect.left + 3, active_rect.bottom - 8}, 1.5f, uiAccent(0.95f));
            }
        }
    }
    for (int i = 0; i < static_cast<int>(view.rows.size()); ++i) {
        const auto& row = view.rows[static_cast<size_t>(i)];
        const auto r = layout.sidebarRow(row, view.sidebar_scroll);
        if (r.bottom < top || r.top > bottom) continue;
        const float cy = (r.top + r.bottom) * 0.5f;
        const bool hover = view.hover_row == i;
        const bool focused = view.keyboard && view.zone == 0 && view.focus_row == i;
        if (row.heading) {
            const bool collapsed = view.collapsed[static_cast<size_t>(row.group)];
            const float collapse_t = view.collapse_progress[static_cast<size_t>(row.group)];
            textLine(upper(tr(groups[static_cast<int>(row.group)])), r.left + 10, cy + 2, r.right - r.left - 60, desktop_label_font_.Get(),
                     gray(hover ? 0.78f : 0.55f));
            if (hover || collapsed || collapse_t > 0.01f)
                desktopGlyph(glyph::chevron_down,
                             r.right - (row.group == Group::collections ? 40 : row.group == Group::library ? 68 : 14), cy + 2, 9, gray(0.6f),
                             -90.0f * collapse_t);
            if (row.group == Group::library) {
                // "+": a game from disk; the arrows: read the libraries again.
                const auto add = layout.headingAdd(row, view.sidebar_scroll), refresh = layout.headingRefresh(row, view.sidebar_scroll);
                const bool add_hover = view.hover == Hit{Target::library_add, i}, refresh_hover = view.hover == Hit{Target::library_refresh, i};
                if (add_hover) desktopFill(add, 6, gray(1, 0.08f));
                if (refresh_hover) desktopFill(refresh, 6, gray(1, 0.08f));
                desktopGlyph(glyph::add, (add.left + add.right) * 0.5f, cy + 1, 11, gray(add_hover ? 0.98f : 0.62f));
                desktopGlyph(glyph::refresh, (refresh.left + refresh.right) * 0.5f, cy + 1, 11,
                             view.refreshing ? uiAccent(0.95f) : gray(refresh_hover ? 0.98f : 0.62f),
                             view.refreshing ? view.refresh_angle * 180.0f / std::numbers::pi_v<float> : 0.0f);
            }
            if (row.group == Group::collections) {
                const auto add = layout.headingAdd(row, view.sidebar_scroll);
                const bool add_hover = view.hover == Hit{Target::sidebar_add, i};
                if (add_hover) desktopFill(add, 6, gray(1, 0.08f));
                desktopGlyph(glyph::add, (add.left + add.right) * 0.5f, cy + 1, 11, gray(add_hover ? 0.98f : 0.62f));
            }
            if (focused) desktopFocusRing(r, 8);
            continue;
        }
        if (!entries || row.entry < 0 || row.entry >= static_cast<int>(entries->size())) continue;
        const auto& entry = (*entries)[static_cast<size_t>(row.entry)];
        const bool active = row.entry == view.active;
        const bool drop = view.drop_row == i;
        const float flash = row.entry == view.drop_flash_entry ? smooth(view.drop_flash) : 0.0f;
        const bool lifted = view.reorder_row == i;
        if (drop) { desktopFill(r, 8, uiAccent(0.16f)); desktopStroke(r, 8, uiAccent(0.9f), 1.5f); }
        else if (active && !view.active_y) desktopFill(r, 8, gray(1, 0.085f));
        else if (hover && !active) desktopFill(r, 8, gray(1, 0.045f));
        if (flash > 0.003f && !drop) {
            desktopFill(r, 8, uiAccent(0.22f * flash));
            desktopStroke(r, 8, uiAccent(0.85f * flash), 1.5f);
        }
        if (active && !drop && !view.active_y) desktopFill({r.left, r.top + 8, r.left + 3, r.bottom - 8}, 1.5f, uiAccent(0.95f));
        const float alpha = lifted ? 0.45f : 1.0f;
        // Narrow: the icon moves to the row's middle and the title and count fade out.
        const float narrow = std::clamp(view.sidebar_narrow, 0.0f, 1.0f);
        const float icon_x = r.left + 20 + ((r.left + r.right) * 0.5f - r.left - 20) * narrow;
        if (!desktopBrand(entry.brand, icon_x, cy, 15, active ? palette::text(alpha) : gray(0.78f, alpha)))
            desktopGlyph(entry.kind == EntryKind::favorites && active ? glyph::star_fill : entry.icon, icon_x, cy, 14,
                         active ? palette::text(alpha) : gray(0.62f, alpha));
        if (focused) desktopFocusRing(r, 8);
        if (narrow > 0.98f) continue;
        const float text_alpha = alpha * (1 - narrow);
        const auto ink = active || drop || flash > 0.2f ? palette::text(text_alpha) : gray(0.80f, text_alpha);
        target_->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, r.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        struct Unclip { ID2D1RenderTarget* target; ~Unclip() { target->PopAxisAlignedClip(); } } unclip{target_.Get()};
        const auto count = std::to_wstring(entry.count);
        const float count_width = textAdvance(count, desktop_small_font_.Get());
        textLine(entry.title, r.left + 38, cy, std::max(1.0f, r.right - r.left - 50 - count_width), active ? desktop_strong_font_.Get() : desktop_font_.Get(), ink);
        if (flash > 0.003f) {
            D2D1_MATRIX_3X2_F base{};
            target_->GetTransform(&base);
            const float scale = 1.0f + 0.35f * std::sin(view.drop_flash * std::numbers::pi_v<float>);
            const float cx = r.right - 10 - count_width * 0.5f;
            target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)) * base);
            textLine(count, r.right - 10 - count_width, cy, count_width + 2, desktop_strong_font_.Get(), uiAccent(text_alpha));
            target_->SetTransform(base);
        } else textLine(count, r.right - 10 - count_width, cy, count_width + 2, desktop_small_font_.Get(), palette::muted(text_alpha));
    }
    if (view.reorder_y >= 0) {
        brush(uiAccent());
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(16, view.reorder_y - 1, side.right - 16, view.reorder_y + 1), 1, 1), solid_.Get());
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(16, view.reorder_y), 3, 3), solid_.Get());
    }
    target_->PopAxisAlignedClip();
    // More rows above or below: the list fades into the panel.
    const float max_scroll = layout.sidebarMaxScroll(view.rows);
    const auto fade = [&](ComPtr<ID2D1LinearGradientBrush>& cached, float y0, float y1) {
        if (auto* gradient = desktopGradient(cached, side_bg, rgb(side_bg.r, side_bg.g, side_bg.b, 0), y0, y1))
            target_->FillRectangle(D2D1::RectF(0, std::min(y0, y1), side.right - 1, std::max(y0, y1)), gradient);
    };
    if (view.sidebar_scroll > 1) fade(desktop_fade_top_, top, top + 18);
    if (view.sidebar_scroll < max_scroll - 1) fade(desktop_fade_bottom_, bottom, bottom - 18);
    drawDesktopCompanion(layout, view);
}

// Her card at the bottom of the sidebar: her face, name and character; her remarks pop out of it.
void Renderer::drawDesktopCompanion(const Layout& layout, const View& view) {
    if (!view.companion_card || !context_->companion) return;
    const auto& her = *context_->companion;
    const auto card = layout.companionCard();
    const bool hover = view.hover.target == Target::companion_card || view.hover.target == Target::companion_bubble;
    desktopFill(card, 12, card_bg);
    if (hover) desktopFill(card, 12, gray(1, 0.04f));
    desktopStroke(card, 12, gray(1, hover ? 0.14f : 0.06f));
    const float cy = (card.top + card.bottom) * 0.5f, face = 24, cx = card.left + 12 + face;
    drawCompanionFace(her.look, her.pose, cx, cy, face, 1, her.costume);
    // Her name and character only: her level's badge lives in her chat (no bar in the sidebar).
    const float text_left = cx + face + 12;
    const auto more = layout.companionMore();
    textLine(her.name, text_left, cy - 9, more.left - text_left - 4, desktop_strong_font_.Get(), palette::text());
    textLine(her.persona, text_left, cy + 10, more.left - text_left - 4, desktop_small_font_.Get(), palette::muted());
    if (view.hover.target == Target::companion_more) desktopFill(more, 8, gray(1, 0.09f));
    desktopGlyph(glyph::more, (more.left + more.right) * 0.5f, cy, 16, palette::text());
    // Her line: a bubble beside the card, over the grid, pointing at her face.
    if (view.companion_bubble && her.bubble_alpha > 0.003f) {
        const auto bubble = drawSpeechBubble(her.bubble, her.bubble_waiting && her.bubble.empty(), card.right + 12, cy - 26,
                                             std::min(380.0f, layout.paneLeft() - card.right - 40), card.right - 6, cy, her.bubble_alpha,
                                             title_h + 8, false);
        companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
    }
}

// ---- Grid ------------------------------------------------------------------------------------

void Renderer::drawDesktopHeader(const Layout& layout, const View& view) {
    const float ty = layout.headerTitleY();
    const float title_room = layout.titleRight() - layout.gridLeft() - 70;
    float title_width = 0;
    if (!view.title_mark.empty() && hasPlatformMark(view.title_mark)) // A studio: its logo is the title.
        title_width = drawPlatformMark(view.title_mark, layout.gridLeft(), ty, 36, std::min(title_room, 360.0f), 1.0f / 0.9f, desktop_heading_font_.Get());
    else {
        title_width = std::min(textAdvance(view.title, desktop_heading_font_.Get()), title_room);
        textLine(view.title, layout.gridLeft(), ty, title_width + 2, desktop_heading_font_.Get(), palette::text());
    }
    if (!view.scanning) {
        const auto count = std::to_wstring(view.count);
        const float w = textAdvance(count, desktop_small_font_.Get()) + 16;
        const Rect pill{layout.gridLeft() + title_width + 12, ty - 11, layout.gridLeft() + title_width + 12 + w, ty + 11};
        desktopFill(pill, 11, gray(1, 0.07f));
        textLine(count, pill.left, ty, w, desktop_small_font_.Get(), gray(0.78f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // Sort, the installed filter and the poster size.
    const auto sort = layout.sortButton();
    const float sort_hover = view.hover.target == Target::sort || (view.menu && view.menu->kind == Menu::Kind::sort) ? 1.0f : 0.0f;
    desktopFill(sort, 8, gray(1, 0.04f + 0.04f * sort_hover));
    desktopStroke(sort, 8, gray(1, 0.085f + 0.06f * sort_hover));
    const float sy = (sort.top + sort.bottom) * 0.5f;
    desktopGlyph(glyph::sort, sort.left + 16, sy, 13, gray(0.72f));
    textLine(view.sort_label, sort.left + 32, sy, sort.right - sort.left - 56, desktop_font_.Get(), gray(0.9f));
    desktopGlyph(glyph::chevron_down, sort.right - 15, sy + 1, 9, gray(0.65f));
    const auto chip = layout.installedChip();
    const bool chip_hover = view.hover.target == Target::installed;
    if (view.installed_only) { desktopFill(chip, 8, uiAccent(0.16f)); desktopStroke(chip, 8, uiAccent(0.75f)); }
    else { desktopFill(chip, 8, gray(1, chip_hover ? 0.08f : 0.04f)); desktopStroke(chip, 8, gray(1, chip_hover ? 0.145f : 0.085f)); }
    const float cy = (chip.top + chip.bottom) * 0.5f;
    desktopGlyph(view.installed_only ? glyph::check : glyph::download, chip.left + 16, cy, 12, view.installed_only ? uiAccent() : gray(0.72f));
    textLine(tr(L"Zainstalowane"), chip.left + 30, cy, chip.right - chip.left - 38, desktop_font_.Get(), gray(0.9f));
    const auto slider = layout.sizeSlider();
    const float y = (slider.top + slider.bottom) * 0.5f, left = slider.left + 7, right = slider.right - 7;
    const float level = levelFromPoster(view.poster), knob = left + (right - left) * level;
    const bool slider_hover = view.hover.target == Target::size_slider;
    desktopFill({left, y - 2, right, y + 2}, 2, gray(1, 0.13f));
    desktopFill({left, y - 2, knob, y + 2}, 2, uiAccent(0.9f));
    brush(gray(0.97f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), slider_hover ? 7.5f : 6.5f, slider_hover ? 7.5f : 6.5f), solid_.Get());
}

void Renderer::drawDesktopEmpty(const Layout& layout, const View& view) {
    const auto m = layout.main();
    const float cx = (m.left + m.right) * 0.5f;
    if (view.scanning) {
        // Loading: the grid's shape, gently pulsing.
        const int columns = layout.columns();
        const int rows = std::max(1, static_cast<int>(layout.viewHeight() / layout.rowStep()) + 1);
        for (int i = 0; i < columns * rows; ++i) {
            const auto p = layout.poster(static_cast<size_t>(i), 0);
            const float pulse = 0.035f + 0.025f * (0.5f + 0.5f * std::sin(elapsed_ * 2.6f - static_cast<float>(i) * 0.35f));
            desktopFill(p, 10, gray(1, pulse));
            desktopFill({p.left, p.bottom + 10, p.left + (p.right - p.left) * 0.8f, p.bottom + 20}, 4, gray(1, pulse * 0.8f));
            desktopFill({p.left, p.bottom + 26, p.left + (p.right - p.left) * 0.45f, p.bottom + 34}, 4, gray(1, pulse * 0.6f));
        }
        return;
    }
    const auto& empty = view.empty;
    if (empty.title.empty()) return;
    const float cy = layout.emptyMessageY(!empty.primary.empty(), !empty.body.empty());
    brush(gray(1, 0.05f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - 40), 32, 32), solid_.Get());
    if (empty.icon) desktopGlyph(empty.icon, cx, cy - 40, 24, gray(0.7f));
    const float width = std::min(460.0f, m.right - m.left - 80);
    textLine(empty.title, cx - width * 0.5f, cy + 14, width, desktop_title_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
    desktopCenteredText(empty.body, cx, cy + 32, width, desktop_font_.Get(), palette::muted(), 3);
    const bool two = !empty.secondary.empty();
    if (!empty.primary.empty())
        desktopButton(layout.emptyButton(0, two), 2, view.hover.target == Target::empty_primary ? 1.0f : 0.0f, view.pressed.target == Target::empty_primary,
                      true, 0, empty.primary);
    if (two)
        desktopButton(layout.emptyButton(1, true), 1, view.hover.target == Target::empty_secondary ? 1.0f : 0.0f, view.pressed.target == Target::empty_secondary,
                      true, 0, empty.secondary);
}

// A store's or console's mark, white like the icons, centred at (cx, cy) in a size × size box; false
// when there is none (or it cannot be built), so the caller draws its icon.
bool Renderer::desktopBrand(std::string_view key, float cx, float cy, float size, D2D1_COLOR_F ink) {
    if (key.empty()) return false;
    auto found = brand_icons_.find(std::string(key));
    if (found == brand_icons_.end()) {
        ComPtr<ID2D1Geometry> geometry;
        if (const auto* icon = brand_icons::find(key)) brand_icons::build(factory_.Get(), *icon, geometry.GetAddressOf());
        found = brand_icons_.emplace(std::string(key), geometry).first; // A failed build stays empty: the icon shows.
    }
    if (!found->second) return false;
    const float scale = size / 24.0f;
    fillBrandGeometry(found->first, found->second.Get(), D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(cx - size * 0.5f, cy - size * 0.5f), ink);
    return true;
}

void Renderer::fillBrandGeometry(const std::string& key, ID2D1Geometry* geometry, const D2D1_MATRIX_3X2_F& placement, D2D1_COLOR_F ink) {
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const auto transform = D2D1::Matrix3x2F::ReinterpretBaseType(&placement)[0] * D2D1::Matrix3x2F::ReinterpretBaseType(&base)[0];
    target_->SetTransform(transform);
    brush(ink);
    bool drawn = false;
    if (realizer_) {
        // The mark tessellated once per device scale (1/96 steps), not on every frame.
        const float device = std::sqrt(transform._11 * transform._11 + transform._12 * transform._12);
        const auto realization_key = key + '@' + std::to_string(std::lround(device * 96));
        if (brand_realizations_.size() >= 256 && !brand_realizations_.contains(realization_key)) brand_realizations_.clear();
        auto& realization = brand_realizations_[realization_key];
        if (!realization) realizer_->CreateFilledGeometryRealization(geometry, D2D1::ComputeFlatteningTolerance(transform), realization.GetAddressOf());
        if (realization) { realizer_->DrawGeometryRealization(realization.Get(), solid_.Get()); drawn = true; }
    }
    if (!drawn) target_->FillGeometry(geometry, solid_.Get());
    target_->SetTransform(base);
}

float Renderer::drawLaunchmeLogo(float left, float cy, float height, D2D1_COLOR_F ink, bool centered, bool mark_only) {
    // Not brand_icons keys, so they never meet a store's mark.
    static const std::string full = "launchme_logo", mark = "launchme_logo_mark";
    const auto& key = mark_only ? mark : full;
    auto found = brand_icons_.find(key);
    if (found == brand_icons_.end()) {
        ComPtr<ID2D1Geometry> geometry;
        if (mark_only) brand_icons::buildLogoMark(factory_.Get(), geometry.GetAddressOf());
        else brand_icons::buildLogo(factory_.Get(), geometry.GetAddressOf());
        found = brand_icons_.emplace(key, geometry).first; // A failed build stays empty: nothing is drawn.
    }
    if (!found->second || height <= 0 || ink.a <= 0) return 0;
    D2D1_RECT_F bounds{};
    if (FAILED(found->second->GetBounds(nullptr, &bounds)) || bounds.bottom <= bounds.top) return 0;
    const float scale = height / (bounds.bottom - bounds.top), width = (bounds.right - bounds.left) * scale;
    if (centered) left -= width * 0.5f;
    fillBrandGeometry(key, found->second.Get(), D2D1::Matrix3x2F::Translation(-bounds.left, -bounds.top) * D2D1::Matrix3x2F::Scale(scale, scale) *
                                                    D2D1::Matrix3x2F::Translation(left, cy - height * 0.5f), ink);
    return width;
}

void Renderer::drawDesktopGrid(const Layout& layout, const View& view) {
    const auto m = layout.main();
    drawDesktopHeader(layout, view);
    target_->PushAxisAlignedClip(D2D1::RectF(m.left, layout.gridTop(), m.right, m.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (!cards_ || cards_->empty()) drawDesktopEmpty(layout, view);
    else {
        const auto [first, end] = layout.visibleRange(cards_->size(), view.scroll, 1);
        // The hovered poster lifts above its neighbours: drawn last.
        size_t lifted = SIZE_MAX;
        for (size_t i = first; i < end; ++i) {
            if (i < view.card_hover.size() && view.card_hover[i] > 0.01f && lifted == SIZE_MAX) { lifted = i; continue; }
            drawDesktopPoster(layout, view, i);
        }
        if (lifted != SIZE_MAX) drawDesktopPoster(layout, view, lifted);
    }
    target_->PopAxisAlignedClip();
    // Scrolled: a soft edge under the header.
    if (view.scroll > 2)
        if (auto* shade = desktopGradient(desktop_shade_, gray(0, 0.32f), gray(0, 0), layout.gridTop(), layout.gridTop() + 16))
            target_->FillRectangle(D2D1::RectF(m.left, layout.gridTop(), m.right, layout.gridTop() + 16), shade);
    if (cards_ && view.scrollbar > 0.01f) {
        const auto thumb = layout.scrollThumb(cards_->size(), view.scroll);
        if (thumb.bottom > thumb.top) {
            const bool hover = view.hover.target == Target::scrollbar;
            const Rect bar{hover ? thumb.left : thumb.left + 2, thumb.top, thumb.right, thumb.bottom};
            desktopFill(bar, 4, gray(1, (hover ? 0.42f : 0.26f) * view.scrollbar));
        }
    }
}

void Renderer::drawDesktopPoster(const Layout& layout, const View& view, size_t index) {
    const auto& card = (*cards_)[index];
    const auto& game = card.game;
    const auto cell = layout.poster(index, view.scroll);
    if (cell.bottom + caption_h < layout.gridTop() || cell.top > layout.height || card.opacity < 0.01f) return;
    const float hover = index < view.card_hover.size() ? view.card_hover[index] : 0.0f;
    const bool selected = index == selected_;
    const bool marked = index < view.marked.size() && view.marked[index];
    const float alpha = card.opacity;
    const float lift = 4 * smooth(hover);
    const auto rect = D2D1::RectF(cell.left, cell.top - lift, cell.right, cell.bottom - lift);
    const Rect poster{rect.left, rect.top, rect.right, rect.bottom};
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64;
    const auto ring_shape = selected || marked ? tileShape(box(inflate(poster, 3.5f + (focus_gap_ != AppSettings::FocusGap::none ? 2.5f : 0.0f))), tile_shape_) : ComPtr<ID2D1Geometry>{};
    // The poster's own shape, shadow and outline: tessellated once for this size, drawn at its place.
    const auto* shaped = posterShape(rect.right - rect.left, rect.bottom - rect.top);
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    if (!shaped) return;
    const float shadow = alpha * (0.35f + 0.65f * std::max(hover, selected ? 0.5f : 0.0f));
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(rect.left, rect.top) * base);
    const auto local = D2D1::RectF(0, 0, rect.right - rect.left, rect.bottom - rect.top);
    auto* shape = shaped->geometry.Get();
    const auto fill = [&](D2D1_COLOR_F ink) {
        brush(ink);
        if (shaped->fill) realizer_->DrawGeometryRealization(shaped->fill.Get(), solid_.Get());
        else target_->FillGeometry(shape, solid_.Get());
    };
    if (shadow > 0.003f) {
        if (shaped->shadow) target_->DrawBitmap(shaped->shadow.Get(), shaped->shadow_box, std::min(1.0f, shadow), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else desktopShadow({0, 0, local.right, local.bottom}, 12, shadow);
    }
    constexpr float tones[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
    const int tone = static_cast<int>(game.app_id % 4);
    fill(rgb(tones[tone][0] * 0.7f, tones[tone][1] * 0.7f, tones[tone][2] * 0.7f, alpha));
    const auto session = sessionOf(game);
    const bool frozen = session == SessionState::frozen;
    // Black and white: a suspended game, and (when chosen) one that is not on the disk.
    const bool colourless = frozen || (view.gray_uninstalled && !game.installed && !game.installation.active() && game.kind != GameKind::catalog);
    Cover* art = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    auto* realized = shaped->fill.Get();
    if (art || cover) {
        const float shown = art ? reveal(*art) : 0.0f;
        if (cover && shown < 1) {
            // A square cover in a poster's frame: whole, over a dimmed fill of itself.
            const float opacity = alpha * card.cover_opacity * reveal(*cover);
            fillImage(*cover, local, 10, opacity, colourless, shape, false, realized);
            fill(rgb(0.008f, 0.011f, 0.015f, 0.62f * opacity));
            fillImage(*cover, local, 10, opacity, colourless, shape, true, realized);
        }
        if (art) fillImage(*art, local, 10, alpha * shown, colourless, shape, false, realized);
    } else if (!(index < posters_.size() && posters_[index].decoding) && !(index < covers_.size() && covers_[index].decoding)) {
        desktopCenteredText(game.name, local.right * 0.5f, local.bottom * 0.38f, local.right - 24, desktop_title_font_.Get(), gray(0.92f, alpha), 4);
    }
    const bool installing = game.installation.active();
    if (!game.installed && !installing && game.kind != GameKind::catalog) fill(gray(0, 0.42f * (1 - 0.6f * hover) * alpha));
    if (frozen) fill(rgb(0.015f, 0.018f, 0.022f, 0.34f * alpha));
    if (installing || session != SessionState::none) drawCardStatusShade(0, 0, local.right, local.bottom, alpha, shape);
    if (selected && view.launch > 0.01f) fill(gray(1, 0.18f * std::sin(smooth(view.launch) * std::numbers::pi_v<float>) * alpha));
    if (perfectGame(game)) { // Every achievement unlocked: a gold edge.
        brush(rgb(perfect_gold.r, perfect_gold.g, perfect_gold.b, 0.95f * alpha));
        target_->DrawGeometry(shape, solid_.Get(), 2.2f);
    } else {
        brush(gray(1, (0.07f + 0.16f * hover) * alpha));
        if (shaped->outline) realizer_->DrawGeometryRealization(shaped->outline.Get(), solid_.Get());
        else target_->DrawGeometry(shape, solid_.Get(), 1);
    }
    target_->SetTransform(base);
    if (ring_shape) {
        brush(uiAccent((selected ? (view.keyboard && view.zone == 1 ? 1.0f : 0.92f) : 0.6f) * alpha));
        target_->DrawGeometry(ring_shape.Get(), solid_.Get(), selected && view.keyboard && view.zone == 1 ? 2.5f : 2.0f);
    }
    // Badges: in play, suspended, downloading, to install.
    if (session != SessionState::none) {
        const auto label = frozen ? std::wstring(tr(L"Wstrzymana")) : std::wstring(tr(L"W grze"));
        const float w = textAdvance(label, desktop_label_font_.Get()) + 28;
        const Rect pill{rect.left + 8, rect.top + 8, rect.left + 8 + w, rect.top + 30};
        desktopFill(pill, 11, gray(0, 0.72f * alpha));
        brush(frozen ? gray(0.8f, alpha) : rgb(0.36f, 0.86f, 0.52f, alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pill.left + 11, (pill.top + pill.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
        textLine(label, pill.left + 20, (pill.top + pill.bottom) * 0.5f, w - 22, desktop_label_font_.Get(), gray(0.96f, alpha));
    }
    if (installing) {
        const auto percent = game.installation.percent();
        const float left = rect.left + 10, right = rect.right - 10, y = rect.bottom - 12;
        desktopFill({left, y - 2, right, y + 2}, 2, gray(1, 0.2f * alpha));
        if (percent) desktopFill({left, y - 2, left + (right - left) * std::clamp(*percent / 100.0f, 0.0f, 1.0f), y + 2}, 2, uiAccent(alpha));
        else {
            const float span = (right - left) * 0.3f, phase = std::fmod(elapsed_ * 0.6f, 1.0f);
            const float x0 = left + (right - left + span) * phase - span;
            desktopFill({std::max(left, x0), y - 2, std::min(right, x0 + span), y + 2}, 2, uiAccent(alpha));
        }
        textLine(installationText(game.installation), left, y - 16, right - left, desktop_label_font_.Get(), gray(0.96f, alpha));
    } else if (!view.platform_badges && !game.installed && game.kind != GameKind::catalog && hover < 0.5f) {
        const float cx = rect.right - 20, cy = rect.top + 20;
        brush(gray(0, 0.6f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 12, 12), solid_.Get());
        desktopGlyph(fileNotChosen(game) ? glyph::folder_open : localFileGame(game.source) ? glyph::remove : glyph::download, cx, cy, 11, gray(0.92f, alpha));
    }
    // The store's or console's mark, top right (Ustawienia → Okno desktopowe); the check box is top left.
    if (view.platform_badges && isGame(game)) {
        auto mark = game.platform.empty() ? std::string_view{} : brand_icons::consoleKey(game.platform);
        if (mark.empty()) mark = brand_icons::sourceKey(game.source);
        if (!mark.empty() && rect.right - rect.left > 80) {
            const Rect chip{rect.right - 33, rect.top + 9, rect.right - 9, rect.top + 33};
            desktopFill(chip, 7, gray(0, 0.62f * alpha));
            desktopBrand(mark, (chip.left + chip.right) * 0.5f, (chip.top + chip.bottom) * 0.5f, 14, gray(0.96f, alpha));
        }
    }
    // On hover: the check box for marking several (playing is the pane's button or a double click).
    const float check_alpha = marked ? 1.0f : std::max(hover, view.marked_count ? 0.55f : 0.0f);
    if (check_alpha > 0.01f && isGame(game)) {
        const auto check = Layout::checkBox(poster);
        const float cx = (check.left + check.right) * 0.5f, cy = (check.top + check.bottom) * 0.5f;
        const bool over = view.hover == Hit{Target::card_check, static_cast<int>(index)};
        const float mark_pulse = index < view.card_mark.size() ? view.card_mark[index] : 0.0f;
        const float pop = std::sin(mark_pulse * std::numbers::pi_v<float>);
        if (marked) {
            const float scale = 1.0f + 0.25f * pop;
            brush(uiAccent(alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11 * scale, 11 * scale), solid_.Get());
            const auto accent = uiAccent();
            const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
            desktopGlyph(glyph::check, cx, cy, 11 * scale, luminance > 0.6f ? gray(0.06f, alpha) : gray(0.98f, alpha));
        } else {
            const float scale = (0.76f + 0.24f * smooth(check_alpha)) * (1.0f - 0.16f * pop);
            brush(gray(0, (over ? 0.62f : 0.45f) * check_alpha * alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11 * scale, 11 * scale), solid_.Get());
            brush(gray(1, (over ? 1.0f : 0.8f) * check_alpha * alpha));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10 * scale, 10 * scale), solid_.Get(), 1.5f);
        }
    }
    // Caption: the name, then where it comes from and what state it is in.
    const float name_y = cell.bottom + 15, meta_y = cell.bottom + 33;
    textLine(game.name, cell.left + 2, name_y, cell.right - cell.left - 4, desktop_strong_font_.Get(), palette::text((selected ? 1.0f : 0.88f) * alpha));
    std::wstring meta = activePlatformName(game);
    if (meta.empty() && !game.version_group.empty()) meta = tr(L"Kilka wersji");
    if (!game.installed && !installing && game.kind != GameKind::catalog)
        meta += (meta.empty() ? L"" : L" · ") + std::wstring(localFileGame(game.source) ? missingFileLabel(game) : tr(L"Do instalacji"));
    textLine(meta, cell.left + 2, meta_y, cell.right - cell.left - 4, desktop_small_font_.Get(), palette::muted(alpha));
}

// ---- Pane ------------------------------------------------------------------------------------

void Renderer::drawDesktopBulk(const Layout& layout, const View& view) {
    const auto p = layout.pane();
    textLine(std::wstring(tr(L"Zaznaczone: ")) + std::to_wstring(view.pane.marked), p.left + 20, title_h + 44, p.right - p.left - 40,
             desktop_title_font_.Get(), palette::text());
    textLine(tr(L"Akcje dla wszystkich zaznaczonych gier"), p.left + 20, title_h + 72, p.right - p.left - 40, desktop_font_.Get(), palette::muted());
    const wchar_t glyphs[] = {glyph::folder, glyph::star, glyph::refresh, glyph::hide, glyph::close};
    const wchar_t* labels[] = {trKey(L"Dodaj do kolekcji…"), trKey(L"Dodaj do ulubionych"), trKey(L"Pobierz grafiki ponownie"), trKey(L"Ukryj zaznaczone"),
                               trKey(L"Wyczyść zaznaczenie")};
    for (int i = 0; i < Layout::bulk_count; ++i) {
        const auto r = layout.paneBulk(i);
        const float hover = view.hover == Hit{Target::bulk, i} ? 1.0f : 0.0f;
        desktopButton(r, i == 0 ? 2 : 1, hover, view.pressed == Hit{Target::bulk, i}, true, glyphs[i], tr(labels[i]));
        if (view.keyboard && view.zone == 2 && view.pane_focus == i) desktopFocusRing(r, 8);
    }
}

void Renderer::drawDesktopPane(const Layout& layout, const View& view) {
    const auto p = layout.pane();
    brush(side_bg);
    target_->FillRectangle(box(p), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left, p.top, p.left + 1, p.bottom), solid_.Get());
    // Its edge under the pointer or dragged: the accent, as wide as the grip.
    if (view.hover.target == Target::pane_resize || view.pressed.target == Target::pane_resize) {
        brush(uiAccent(view.pressed.target == Target::pane_resize ? 0.95f : 0.6f));
        target_->FillRectangle(D2D1::RectF(p.left - 1, p.top, p.left + 2, p.bottom), solid_.Get());
    }
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, p.top, p.right, p.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto& pane = view.pane;
    const float left = p.left + 20, width = p.right - p.left - 40;
    if (pane.marked > 1) { drawDesktopBulk(layout, view); target_->PopAxisAlignedClip(); desktop_pane_height_ = 0; return; }
    if (!pane.game || !cards_ || selected_ >= cards_->size()) {
        const float cx = (p.left + p.right) * 0.5f, cy = (p.top + p.bottom) * 0.42f;
        brush(gray(1, 0.045f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 30, 30), solid_.Get());
        desktopGlyph(glyph::game, cx, cy, 22, gray(0.6f));
        textLine(view.scanning ? tr(L"Wczytywanie biblioteki…") : view.library_empty ? tr(L"Biblioteka jest pusta") : tr(L"Wybierz grę"),
                 p.left + 20, cy + 52, width, desktop_strong_font_.Get(), gray(0.8f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        target_->PopAxisAlignedClip();
        desktop_pane_height_ = 0;
        return;
    }
    const float s = view.pane_scroll;
    // The picture: the game's background (cross-faded on a new selection), else its blurred cover.
    const auto banner = box(layout.banner(s));
    ID2D1Bitmap* hero = selected_ < heroes_.size() ? heroes_[selected_].bitmap.Get() : nullptr;
    if (hero != desktop_hero_.Get()) {
        desktop_hero_previous_ = desktop_hero_;
        desktop_hero_ = hero;
        desktop_hero_changed_ = std::chrono::steady_clock::now();
    }
    const float fade = smooth(std::chrono::duration<float>(std::chrono::steady_clock::now() - desktop_hero_changed_).count() / 0.28f);
    if (!hero && selected_ < covers_.size() && covers_[selected_].bitmap)
        if (auto* tint = blurredArt(covers_[selected_])) target_->DrawBitmap(tint, banner, 0.85f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (desktop_hero_previous_ && fade < 1) desktopBitmapFill(desktop_hero_previous_.Get(), banner, 1 - fade);
    if (hero) desktopBitmapFill(hero, banner, fade);
    // After the delay on one game (Filmy w tle), its video plays in the picture's place.
    drawVideoIn(banner, nullptr, 1);
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> shade;
        const D2D1_GRADIENT_STOP shade_stops[] = {{0, rgb(side_bg.r, side_bg.g, side_bg.b, 0.15f)}, {0.45f, rgb(side_bg.r, side_bg.g, side_bg.b, 0.05f)},
                                                  {1, side_bg}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, banner.top), D2D1::Point2F(0, banner.bottom)),
                                                        stops.Get(), shade.GetAddressOf())))
            target_->FillRectangle(banner, shade.Get());
    }
    // Its logo over the picture's foot.
    if (selected_ < logos_.size() && logos_[selected_].bitmap) {
        const auto& logo = logos_[selected_];
        const auto pixels = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w > 0 && h > 0) {
            const float max_w = std::min(width * 0.72f, 260.0f), max_h = std::min(76.0f, (banner.bottom - banner.top) * 0.4f);
            const float k = std::min(max_w / w, max_h / h);
            const auto r = D2D1::RectF(left, banner.bottom - 16 - h * k, left + w * k, banner.bottom - 16);
            target_->DrawBitmap(logo.bitmap.Get(), r, reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        }
    }
    // Name, origin and state.
    textLine(pane.name, left, layout.paneY(22, s), width, desktop_title_font_.Get(), palette::text());
    float x = left;
    const float meta_y = layout.paneY(50, s);
    if (!pane.platform.empty()) {
        const float w = std::min(textAdvance(pane.platform, desktop_small_font_.Get()) + 18, width * 0.6f);
        const Rect pill{x, meta_y - 11, x + w, meta_y + 11};
        desktopStroke(pill, 11, gray(1, 0.16f));
        textLine(pane.platform, pill.left + 9, meta_y, w - 16, desktop_small_font_.Get(), gray(0.85f));
        x = pill.right + 10;
    }
    const auto state_ink = pane.running ? rgb(0.36f, 0.86f, 0.52f) : pane.progress || pane.launching ? uiAccent() : palette::muted();
    if (pane.running || pane.frozen) {
        brush(state_ink);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 4, meta_y), 3.5f, 3.5f), solid_.Get());
        x += 13;
    }
    textLine(pane.launching ? std::wstring(tr(L"Uruchamianie…")) : pane.status, x, meta_y, left + width - x, desktop_small_font_.Get(), state_ink);
    // Buttons.
    const auto primary = layout.panePrimary(s);
    const float primary_hover = view.hover.target == Target::pane_primary ? 1.0f : 0.0f;
    const bool pressed = view.pressed.target == Target::pane_primary;
    if (pane.progress) {
        desktopInstallButton(primary, *pane.progress, pane.primary);
    } else {
        const wchar_t g = !pane.installed && !pane.running && !pane.frozen ? glyph::download : pane.frozen ? glyph::play : pane.running ? glyph::open : glyph::play;
        desktopButton(primary, 2, primary_hover, pressed, pane.primary_enabled, g, pane.primary);
    }
    const auto favorite = layout.paneFavorite(s);
    desktopButton(favorite, 1, view.hover.target == Target::pane_favorite ? 1.0f : 0.0f, view.pressed.target == Target::pane_favorite, true, 0, {});
    const float fav_scale = 1.0f + (pane.favorite ? 0.32f : -0.18f) * std::sin(pane.favorite_pulse * std::numbers::pi_v<float>);
    desktopGlyph(pane.favorite ? glyph::star_fill : glyph::star, (favorite.left + favorite.right) * 0.5f, (favorite.top + favorite.bottom) * 0.5f,
                 16 * fav_scale, pane.favorite ? rgb(1.0f, 0.78f, 0.28f) : gray(0.85f));
    const auto more = layout.paneMore(s);
    const bool more_open = view.menu && view.menu->align_right && view.menu->kind == Menu::Kind::game;
    desktopButton(more, 1, view.hover.target == Target::pane_more || more_open ? 1.0f : 0.0f, view.pressed.target == Target::pane_more, true, glyph::more, {});
    if (view.keyboard && view.zone == 2) {
        if (view.pane_focus == 0) desktopFocusRing(primary, 8);
        else if (view.pane_focus == 1) desktopFocusRing(favorite, 8);
        else if (view.pane_focus == 2) desktopFocusRing(more, 8);
    }
    // Numbers.
    const auto stats = layout.paneStats(s);
    desktopFill(stats, 10, card_bg);
    desktopStroke(stats, 10, gray(1, 0.05f));
    const float column = (stats.right - stats.left) / 3;
    const auto stat = [&](int i, const wchar_t* label, const std::wstring& value, float share, bool link = false) {
        const float cx = stats.left + column * static_cast<float>(i) + 14;
        if (link) { // The number opens its list: hover, press and a chevron.
            const Rect cell{cx - 10, stats.top + 4, stats.left + column * static_cast<float>(i + 1) - 4, stats.bottom - 4};
            const bool hover = view.hover.target == Target::pane_achievements;
            if (hover) desktopFill(cell, 7, gray(1, view.pressed.target == Target::pane_achievements ? 0.04f : 0.07f));
            desktopGlyph(glyph::chevron_right, cell.right - 12, stats.top + 43, 9, gray(hover ? 0.95f : 0.55f));
            desktop_achievements_ = cell;
        }
        textLine(upper(tr(label)), cx, stats.top + 20, column - 20, desktop_label_font_.Get(), palette::muted());
        textLine(value.empty() ? L"—" : value, cx, stats.top + 43, column - (link ? 36 : 20), desktop_strong_font_.Get(), palette::text());
        if (share >= 0) {
            desktopFill({cx, stats.bottom - 9, cx + column - 28, stats.bottom - 6}, 1.5f, gray(1, 0.1f));
            desktopFill({cx, stats.bottom - 9, cx + (column - 28) * share, stats.bottom - 6}, 1.5f, uiAccent());
        }
        if (i) { brush(gray(1, 0.06f)); target_->FillRectangle(D2D1::RectF(cx - 14, stats.top + 12, cx - 13, stats.bottom - 12), solid_.Get()); }
    };
    stat(0, trKey(L"Czas gry"), pane.playtime, -1);
    stat(1, trKey(L"Ostatnio"), pane.last_played, -1);
    stat(2, trKey(L"Osiągnięcia"), pane.achievements, pane.achievements_share, pane.achievements_link);
    // Quick actions.
    const wchar_t quick_glyphs[] = {glyph::info, glyph::picture, glyph::folder_open, pane.hidden ? glyph::view : glyph::hide};
    const std::wstring quick_labels[] = {tr(L"Szczegóły"), tr(L"Grafika"), tr(L"Folder"), pane.hidden ? std::wstring(tr(L"Pokaż")) : std::wstring(tr(L"Ukryj"))};
    for (int i = 0; i < Layout::quick_count; ++i) {
        const auto r = layout.paneQuick(i, s);
        const bool enabled = (i != 2 || pane.folder) && (i != 3 || pane.hideable);
        const bool hover = enabled && view.hover == Hit{Target::pane_quick, i};
        desktopFill(r, 9, gray(1, hover ? 0.075f : 0.035f));
        if (hover && view.pressed == Hit{Target::pane_quick, i}) desktopFill(r, 9, gray(1, 0.04f));
        const auto ink = enabled ? gray(hover ? 0.98f : 0.82f) : gray(0.38f);
        desktopGlyph(quick_glyphs[i], (r.left + r.right) * 0.5f, r.top + 20, 15, ink);
        textLine(quick_labels[i], r.left + 4, r.top + 40, r.right - r.left - 8, desktop_small_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (view.keyboard && view.zone == 2 && view.pane_focus == 3 + i) desktopFocusRing(r, 9);
    }
    // Collections, as chips that open them; "+" adds the game to one.
    float y = layout.paneContentTop(s);
    if (!context_->aurelia_cloud.empty()) {
        // Cloud saves (a game Aurelia manages): the state, and a click opens its page.
        const float w = std::min(width, textAdvance(context_->aurelia_cloud, desktop_small_font_.Get()) + 44);
        const Rect chip{left - 6, y - 13, left - 6 + w, y + 13};
        const bool hover = view.hover.target == Target::pane_cloud;
        if (hover) desktopFill(chip, 7, gray(1, view.pressed.target == Target::pane_cloud ? 0.04f : 0.07f));
        desktopGlyph(0xE753, chip.left + 16, y, 14, gray(hover ? 0.98f : 0.78f)); // Segoe MDL2 "Cloud".
        textLine(context_->aurelia_cloud, chip.left + 32, y, w - 38, desktop_small_font_.Get(), gray(hover ? 0.98f : 0.78f));
        desktop_cloud_ = chip;
        y += 30;
    }
    textLine(upper(tr(L"Kolekcje")), left, y, width, desktop_label_font_.Get(), palette::muted());
    y += 16;
    float cx = left;
    const float chip_h = 26;
    for (int i = 0; i < static_cast<int>(pane.collections.size()); ++i) {
        const auto& name = pane.collections[static_cast<size_t>(i)].first;
        const float w = std::min(textAdvance(name, desktop_small_font_.Get()) + 22, width);
        if (cx + w > left + width && cx > left) { cx = left; y += chip_h + 6; }
        const Rect chip{cx, y, cx + w, y + chip_h};
        const bool hover = view.hover == Hit{Target::pane_chip, i};
        desktopFill(chip, chip_h * 0.5f, gray(1, hover ? 0.11f : 0.06f));
        desktopStroke(chip, chip_h * 0.5f, gray(1, 0.09f));
        textLine(name, chip.left + 11, (chip.top + chip.bottom) * 0.5f, w - 20, desktop_small_font_.Get(), gray(0.9f));
        desktop_chips_.push_back(chip);
        cx = chip.right + 6;
    }
    {
        const auto label = pane.collections.empty() ? std::wstring(tr(L"Dodaj do kolekcji")) : std::wstring(tr(L"Dodaj"));
        const float w = textAdvance(label, desktop_small_font_.Get()) + 36;
        if (cx + w > left + width && cx > left) { cx = left; y += chip_h + 6; }
        const Rect chip{cx, y, cx + w, y + chip_h};
        const bool hover = view.hover.target == Target::pane_chip_add;
        desktopFill(chip, chip_h * 0.5f, hover ? uiAccent(0.16f) : gray(1, 0.0f));
        desktopStroke(chip, chip_h * 0.5f, hover ? uiAccent(0.8f) : gray(1, 0.16f));
        desktopGlyph(glyph::add, chip.left + 14, (chip.top + chip.bottom) * 0.5f, 10, hover ? uiAccent() : gray(0.8f));
        textLine(label, chip.left + 25, (chip.top + chip.bottom) * 0.5f, w - 30, desktop_small_font_.Get(), hover ? palette::text() : gray(0.85f));
        desktop_chip_add_ = chip;
        y += chip_h + 22;
    }
    // The rating link, the description and the facts.
    if (!pane.rating.empty()) {
        const auto label = pane.rating + L"  \u2197";
        const float w = std::min(textAdvance(label, desktop_font_.Get()) + 4, width);
        const bool hover = view.hover.target == Target::pane_rating;
        textLine(label, left, y + 8, w, desktop_font_.Get(), uiAccent(hover ? 1.0f : 0.85f));
        if (hover) { brush(uiAccent(0.6f)); target_->FillRectangle(D2D1::RectF(left, y + 17, left + w, y + 18), solid_.Get()); }
        desktop_rating_ = {left, y - 2, left + w, y + 20};
        y += 34;
    }
    textLine(upper(tr(L"Opis")), left, y, width, desktop_label_font_.Get(), palette::muted());
    y += 14;
    const auto& details = pane.details;
    if (!details.description.empty()) y += paragraph(details.description, left, y, width, desktop_font_.Get(), gray(0.80f), 60) + 18;
    else {
        if (pane.details_loading) drawSkeleton(D2D1::RectF(left, y, left + width, y + 66), 3, 22);
        else textLine(tr(L"Opis gry nie jest dostępny."), left, y + 9, width, desktop_font_.Get(), palette::muted());
        y += 36;
    }
    std::vector<std::pair<const wchar_t*, const std::wstring*>> facts;
    const std::pair<const wchar_t*, const std::wstring*> all_facts[] = {
        {trKey(L"Developer"), &details.developer}, {trKey(L"Wydawca"), &details.publisher}, {trKey(L"Premiera"), &details.release_date},
        {trKey(L"Gatunek"), &details.genres}, {trKey(L"Gracze"), &details.players}};
    for (const auto& row : all_facts) if (!row.second->empty()) facts.push_back(row);
    if (!facts.empty()) {
        textLine(upper(tr(L"Informacje")), left, y, width, desktop_label_font_.Get(), palette::muted());
        y += 14;
        const float label_width = std::min(96.0f, width * 0.34f);
        for (const auto& [label, value] : facts) {
            textLine(tr(label), left, y + 12, label_width, desktop_font_.Get(), palette::muted());
            // Developer and Wydawca: links to the studio's games (its section, or the search for it).
            const int studio = value == &details.developer ? 0 : value == &details.publisher ? 1 : -1;
            const bool hover = studio >= 0 && view.hover == Hit{Target::pane_studio, studio};
            const auto ink = studio < 0 ? gray(0.88f) : uiAccent(hover ? 1.0f : 0.85f);
            const float value_left = left + label_width + 10, value_top = y + 12 - typeMetrics(desktop_font_.Get()).baseline + typeMetrics(desktop_font_.Get()).cap * 0.5f;
            const float h = paragraph(*value, value_left, value_top, width - label_width - 10, desktop_font_.Get(), ink, 3);
            if (studio >= 0) {
                const float w = std::min(textAdvance(*value, desktop_font_.Get()) + 2, width - label_width - 10);
                desktop_studios_[static_cast<size_t>(studio)] = {value_left - 2, y, value_left + w + 2, y + std::max(24.0f, h + 4)};
                if (h < 30) { // Underlined: it opens the studio's games (stronger under the pointer).
                    brush(hover ? uiAccent(0.7f) : gray(1, 0.22f));
                    target_->FillRectangle(D2D1::RectF(value_left, y + 21, value_left + w, y + 22), solid_.Get());
                }
            }
            y += std::max(26.0f, h + 6);
        }
    }
    desktop_pane_height_ = y + s - title_h + 28;
    target_->PopAxisAlignedClip();
    // A long page shows how far it has scrolled.
    const float page = layout.height - title_h;
    if (desktop_pane_height_ > page + 1) {
        const float track = page - 12, length = std::max(30.0f, track * page / desktop_pane_height_);
        const float top = title_h + 6 + (track - length) * std::clamp(s / (desktop_pane_height_ - page), 0.0f, 1.0f);
        desktopFill({p.right - 6, top, p.right - 3, top + length}, 1.5f, gray(1, 0.14f));
    }
}
