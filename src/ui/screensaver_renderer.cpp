// Screensaver drawing: game backgrounds with a slow Ken Burns move, game boxes turning in
// perspective above a soft floor reflection, and spinning discs. Every opacity is
// multiplied by saver_alpha_, the intro fade above the launcher screen.
#include "renderer.hpp"
#include "perspective.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using Item = screensaver::View::Item;
using perspective::Vec3, perspective::Camera, perspective::Face, perspective::turned, perspective::cropTo, perspective::dot,
    perspective::normalized;

namespace {
constexpr float pi = std::numbers::pi_v<float>;
D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
// 0..1 from eight bits of a seed.
float unit(uint32_t seed, int shift) { return static_cast<float>((seed >> shift) & 0xFF) / 255.0f; }

// ±25° around the vertical axis, one slow swing every 14 s.
float boxAngle(const Item& item) {
    return 25.0f * pi / 180.0f * std::sin(2 * pi * item.age / 14.0f + static_cast<float>(item.seed % 628) / 100.0f);
}
} // namespace

void Renderer::setScreensaverImage(int slot, std::vector<uint8_t> bytes, bool hero) {
    if (slot < 0 || slot >= screensaver_slots) return;
    auto& image = saver_slots_[slot];
    image = Cover{};
    image.hero = hero; // Heroes are downsampled to 1920 px; posters and covers get an accent colour.
    image.pixels = bytes.empty() ? ImageBytes{} : std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    createBitmap(image);
}

void Renderer::clearScreensaver() {
    for (auto& image : saver_slots_) image = Cover{};
    saver_glows_.clear();
}

Renderer::SlotState Renderer::screensaverSlot(int slot) const {
    if (slot < 0 || slot >= screensaver_slots) return SlotState::empty;
    const auto& image = saver_slots_[slot];
    if (!image.loaded()) return SlotState::empty;
    if (image.bitmap) return SlotState::ready;
    if (image.decoding) return SlotState::pending;
    // Uploaded without a bitmap: corrupt data. Without a target nothing was tried yet;
    // the first frame decodes it.
    return target_ ? SlotState::failed : SlotState::ready;
}

bool Renderer::createSaverResources() {
    if (!saver_layer_ && FAILED(target_->CreateLayer(saver_layer_.GetAddressOf()))) return false;
    const auto linear = [&](ComPtr<ID2D1LinearGradientBrush>& brush, const D2D1_GRADIENT_STOP* stops, UINT count) {
        if (brush) return true;
        ComPtr<ID2D1GradientStopCollection> collection;
        return SUCCEEDED(target_->CreateGradientStopCollection(stops, count, collection.GetAddressOf())) &&
               SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}),
                                                            collection.Get(), brush.GetAddressOf()));
    };
    const D2D1_GRADIENT_STOP mask[] = {{0, color(0, 0, 0, 1)}, {1, color(0, 0, 0, 0)}};
    const D2D1_GRADIENT_STOP sheen[] = {{0, color(1, 1, 1, 0)}, {0.5f, color(1, 1, 1, 1)}, {1, color(1, 1, 1, 0)}};
    const D2D1_GRADIENT_STOP shade[] = {{0, color(0, 0, 0, 0)}, {1, color(0, 0, 0, 1)}};
    if (!linear(saver_mask_, mask, 2) || !linear(saver_sheen_, sheen, 3) || !linear(saver_shade_, shade, 2)) return false;
    if (!saver_shadow_) {
        const D2D1_GRADIENT_STOP stops[] = {{0, color(0, 0, 0, 1)}, {0.55f, color(0, 0, 0, 0.42f)}, {1, color(0, 0, 0, 0)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) ||
            FAILED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1),
                                                      collection.Get(), saver_shadow_.GetAddressOf()))) return false;
    }
    return true;
}

void Renderer::drawScreensaver(const screensaver::View& view) {
    saver_alpha_ = std::clamp(view.opacity, 0.0f, 1.0f);
    const float width = viewport_.width, height = viewport_.height;
    const auto base = D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale);
    target_->SetTransform(base);
    brush(color(0.008f, 0.009f, 0.011f, saver_alpha_));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    if (!createSaverResources()) return;
    if (view.empty) {
        // Nothing to show: a dim screen and one soft accent glow drifting over minutes.
        drawSaverGlow(uiAccent(), width * (0.5f + 0.30f * std::sin(view.time * 0.045f)),
                      height * (0.5f + 0.24f * std::sin(view.time * 0.031f + 1.3f)), width * 0.42f, 0.16f);
        return;
    }
    const auto draw = [&](const Item& item, float alpha) {
        if (item.slot < 0 || item.slot >= screensaver_slots || alpha <= 0.002f) return;
        switch (view.style) {
        case screensaver::Style::backgrounds: drawSaverHero(item, alpha); break;
        case screensaver::Style::boxes: drawSaverBox(item, alpha); break;
        case screensaver::Style::discs: drawSaverDisc(item, alpha); break;
        }
        target_->SetTransform(base);
    };
    if (view.style == screensaver::Style::backgrounds) {
        // Opaque full-screen images: the outgoing one stays fully drawn beneath, so the
        // crossfade never dips through black.
        draw(view.previous, 1.0f);
        draw(view.current, view.current.alpha);
        saver_shade_->SetStartPoint({0, height - 300});
        saver_shade_->SetEndPoint({0, height});
        saver_shade_->SetOpacity(0.62f * saver_alpha_);
        target_->FillRectangle(D2D1::RectF(0, height - 300, width, height), saver_shade_.Get());
    } else {
        draw(view.previous, view.previous.alpha);
        draw(view.current, view.current.alpha);
    }
    // Titles share one spot: the old one fades out before the new one fades in.
    drawSaverTitle(view.previous.title, std::clamp(view.previous.alpha * 2 - 1, 0.0f, 1.0f));
    drawSaverTitle(view.current.title, view.previous.slot < 0 ? view.current.alpha : std::clamp(view.current.alpha * 2 - 1, 0.0f, 1.0f));
}

void Renderer::drawSaverGlow(D2D1_COLOR_F ink, float cx, float cy, float radius, float alpha) {
    const uint32_t key = static_cast<uint32_t>(std::clamp(ink.r, 0.0f, 1.0f) * 255) << 16 |
                         static_cast<uint32_t>(std::clamp(ink.g, 0.0f, 1.0f) * 255) << 8 |
                         static_cast<uint32_t>(std::clamp(ink.b, 0.0f, 1.0f) * 255);
    auto found = saver_glows_.find(key);
    if (found == saver_glows_.end()) {
        if (saver_glows_.size() >= 8) saver_glows_.clear(); // A few colours are on screen at once.
        const D2D1_GRADIENT_STOP stops[] = {{0, color(ink.r, ink.g, ink.b, 1)}, {0.45f, color(ink.r, ink.g, ink.b, 0.38f)},
                                            {1, color(ink.r, ink.g, ink.b, 0)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1RadialGradientBrush> glow;
        if (FAILED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) ||
            FAILED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1),
                                                      collection.Get(), glow.GetAddressOf()))) return;
        found = saver_glows_.emplace(key, std::move(glow)).first;
    }
    auto* glow = found->second.Get();
    glow->SetCenter({cx, cy});
    glow->SetRadiusX(radius);
    glow->SetRadiusY(radius);
    glow->SetOpacity(alpha * saver_alpha_);
    target_->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), glow);
}

// Full screen with a slow pan and zoom; direction and path vary per game.
void Renderer::drawSaverHero(const Item& item, float alpha) {
    const auto& image = saver_slots_[item.slot];
    if (!image.bitmap) return;
    const auto size = image.bitmap->GetSize();
    const float width = viewport_.width, height = viewport_.height;
    const float t = std::clamp(item.age / (screensaver::item_seconds + screensaver::fade_seconds + 1.5f), 0.0f, 1.0f);
    const bool zoom_in = (item.seed & 1) != 0;
    const float zoom = zoom_in ? std::lerp(1.05f, 1.17f, t) : std::lerp(1.17f, 1.05f, t);
    const float factor = std::max(width / size.width, height / size.height) * zoom;
    const float w = size.width * factor, h = size.height * factor;
    const float from_x = 0.2f + 0.6f * unit(item.seed, 8), from_y = 0.25f + 0.5f * unit(item.seed, 16);
    const float to_x = 1 - from_x, to_y = 0.25f + 0.5f * unit(item.seed, 24);
    const float left = -(w - width) * std::lerp(from_x, to_x, t), top = -(h - height) * std::lerp(from_y, to_y, t);
    target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(left, top, left + w, top + h), alpha * saver_alpha_,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

// A game case: the poster on the front, its edge wrapped onto the spine, turning in
// perspective (ID2D1DeviceContext, Windows 8+) above a fading floor reflection.
void Renderer::drawSaverBox(const Item& item, float alpha) {
    auto& image = saver_slots_[item.slot];
    if (!image.bitmap) return;
    if (!saver_context_checked_) { saver_context_checked_ = true; target_.As(&saver_context_); }
    bool flat = !saver_context_;
#ifdef LAUNCHME_UI_TEST
    flat = flat || flat_boxes_for_test;
#endif
    perspective_boxes_ = !flat;
    if (flat) { drawSaverFlatBox(item, alpha); return; }
    auto* context = saver_context_.Get();
    const float scale = viewport_.scale, a = alpha * saver_alpha_;
    const float settle = 0.96f + 0.04f * alpha; // Arrives slightly small, leaves shrinking.
    const float height = viewport_.height * 0.56f * scale * settle;
    const float width = height * (item.poster ? 2.0f / 3.0f : 1.0f);
    const float depth = width * (item.poster ? 0.085f : 0.075f);
    const float angle = boxAngle(item);
    const Camera camera{viewport_.width * 0.5f * scale, viewport_.height * 0.45f * scale, height * 2.1f};
    const float w2 = width / 2, h2 = height / 2, d2 = depth / 2;
    const auto face = [&](Vec3 origin, Vec3 u, Vec3 v) { return Face{turned(origin, angle), turned(u, angle), turned(v, angle)}; };
    const Face front = face({-w2, -h2, -d2}, {width, 0, 0}, {0, height, 0});
    // The spine runs from the front edge backwards, so the cover art wraps around the corner.
    const Face spine = face({-w2, -h2, -d2}, {0, 0, depth}, {0, height, 0});
    const Face opening = face({w2, -h2, -d2}, {0, 0, depth}, {0, height, 0});
    const Vec3 eye{0, 0, -camera.eye};
    const auto facing = [&](const Face& side, Vec3 outward) { return dot(turned(outward, angle), eye - side.centre()) > 0; };
    const bool spine_visible = facing(spine, {-1, 0, 0}), opening_visible = facing(opening, {1, 0, 0});
    const Vec3 light = normalized({-0.45f, -0.35f, -1.0f});
    const auto darkness = [&](Vec3 outward, float base) {
        return std::clamp(base + (1 - std::max(0.0f, dot(turned(outward, angle), light))) * 0.6f, 0.0f, 0.92f);
    };
    const auto size = image.bitmap->GetSize();
    const auto source = cropTo(size, width / height);
    const D2D1_RECT_F strip{source.left, source.top, source.left + (source.right - source.left) * 0.07f, source.bottom};
    const auto quad = [&](const Face& side) {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return ComPtr<ID2D1PathGeometry>{};
        sink->BeginFigure(camera.project(side.origin), D2D1_FIGURE_BEGIN_FILLED);
        const D2D1_POINT_2F corners[] = {camera.project(side.origin + side.u), camera.project(side.origin + side.u + side.v),
                                         camera.project(side.origin + side.v)};
        sink->AddLines(corners, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
    };
    const auto fill = [&](ID2D1PathGeometry* path, D2D1_COLOR_F ink) {
        if (path) { brush(ink); target_->FillGeometry(path, solid_.Get()); }
    };
    const auto picture = [&](const Face& side, const D2D1_RECT_F& part, float opacity) {
        const float w = part.right - part.left, h = part.bottom - part.top;
        const auto matrix = perspective::matrix(side, w, h, camera);
        context->DrawBitmap(image.bitmap.Get(), D2D1::RectF(0, 0, w, h), opacity, D2D1_INTERPOLATION_MODE_LINEAR, &part, &matrix);
    };
    const float border = 0.018f; // Plastic around the printed sleeve.
    // The visible faces, shaded by the light; the reflection is darker still. Returns the front outline.
    const auto sides = [&](bool reflection, float opacity) {
        const float dim = reflection ? 0.2f : 0.0f;
        const auto place = [&](const Face& side) { return reflection ? side.mirrored(h2) : side; };
        if (spine_visible) {
            picture(place(spine), strip, opacity);
            fill(quad(place(spine)).Get(), color(0, 0, 0, (darkness({-1, 0, 0}, 0.30f) + dim) * opacity));
        }
        if (opening_visible) {
            const float lit = 1 - darkness({1, 0, 0}, 0.15f + dim);
            fill(quad(place(opening)).Get(), color(0.05f * lit, 0.055f * lit, 0.065f * lit, opacity));
        }
        const auto outline = quad(place(front));
        fill(outline.Get(), color(0.045f, 0.05f, 0.058f, opacity));
        picture(place(front).inset(border, border * width / height), source, opacity);
        fill(outline.Get(), color(0, 0, 0, (darkness({0, 0, -1}, 0) + dim) * opacity));
        return outline;
    };
    target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    drawSaverGlow(image.accent, viewport_.width * 0.5f, viewport_.height * 0.45f, viewport_.height * 0.62f, 0.20f * alpha);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    // Contact shadow and the reflection on a glossy floor, fading out below the box.
    const float floor = camera.project({0, h2, 0}).y;
    saver_shadow_->SetCenter({camera.cx, floor});
    saver_shadow_->SetRadiusX(width * 0.66f);
    saver_shadow_->SetRadiusY(depth * 1.4f + 10 * scale);
    saver_shadow_->SetOpacity(0.8f * a);
    target_->FillEllipse(D2D1::Ellipse({camera.cx, floor}, width * 0.66f, depth * 1.4f + 10 * scale), saver_shadow_.Get());
    saver_mask_->SetStartPoint({0, floor});
    saver_mask_->SetEndPoint({0, floor + height * 0.34f});
    saver_mask_->SetOpacity(0.30f * a);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1::IdentityMatrix(), 1.0f, saver_mask_.Get()), saver_layer_.Get());
    sides(true, 1.0f);
    target_->PopLayer();
    // A soft highlight that slides across the sleeve as the box turns.
    if (const auto path = sides(false, a)) {
        const auto top_left = camera.project(front.origin), bottom_right = camera.project(front.origin + front.u + front.v);
        const float centre = 0.5f - angle / (25.0f * pi / 180.0f) * 0.42f;
        const auto along = [&](float t) {
            return D2D1::Point2F(std::lerp(top_left.x, bottom_right.x, t), std::lerp(top_left.y, bottom_right.y, t));
        };
        saver_sheen_->SetStartPoint(along(centre - 0.24f));
        saver_sheen_->SetEndPoint(along(centre + 0.24f));
        saver_sheen_->SetOpacity(0.09f * a);
        target_->FillGeometry(path.Get(), saver_sheen_.Get());
    }
}

// Without ID2D1DeviceContext (e.g. a WIC capture target): a flat case squeezed and skewed
// with the same swing, its spine or opening edge beside it and the same reflection.
void Renderer::drawSaverFlatBox(const Item& item, float alpha) {
    auto& image = saver_slots_[item.slot];
    const float a = alpha * saver_alpha_;
    const float settle = 0.96f + 0.04f * alpha;
    const float height = viewport_.height * 0.56f * settle;
    const float width = height * (item.poster ? 2.0f / 3.0f : 1.0f);
    const float depth = width * (item.poster ? 0.085f : 0.075f);
    const float cx = viewport_.width * 0.5f, cy = viewport_.height * 0.45f;
    const float angle = boxAngle(item);
    const auto base = D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale);
    const D2D1_POINT_2F centre{cx, cy};
    const auto box = D2D1::Matrix3x2F::Scale(std::cos(angle), 1, centre) * D2D1::Matrix3x2F::Skew(0, angle * 180 / pi * 0.16f, centre);
    const auto rect = D2D1::RectF(cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2);
    const float border = width * 0.018f;
    const auto inner = D2D1::RectF(rect.left + border, rect.top + border, rect.right - border, rect.bottom - border);
    const auto source = cropTo(image.bitmap->GetSize(), width / height);
    const D2D1_RECT_F strip{source.left, source.top, source.left + (source.right - source.left) * 0.07f, source.bottom};
    const float side = depth * std::abs(std::sin(angle)) * 1.6f / std::cos(angle);
    const auto body = [&](float opacity) {
        if (angle < -0.02f) { // Spine towards the viewer.
            const auto spine = D2D1::RectF(rect.left - side, rect.top, rect.left, rect.bottom);
            target_->DrawBitmap(image.bitmap.Get(), spine, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &strip);
            brush(color(0, 0, 0, 0.45f * opacity));
            target_->FillRectangle(spine, solid_.Get());
        } else if (angle > 0.02f) {
            brush(color(0.05f, 0.055f, 0.065f, opacity));
            target_->FillRectangle(D2D1::RectF(rect.right, rect.top, rect.right + side, rect.bottom), solid_.Get());
        }
        brush(color(0.045f, 0.05f, 0.058f, opacity));
        target_->FillRectangle(rect, solid_.Get());
        target_->DrawBitmap(image.bitmap.Get(), inner, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    };
    target_->SetTransform(base);
    drawSaverGlow(image.accent, cx, cy, viewport_.height * 0.62f, 0.20f * alpha);
    const float floor = rect.bottom;
    saver_shadow_->SetCenter({cx, floor});
    saver_shadow_->SetRadiusX(width * 0.66f);
    saver_shadow_->SetRadiusY(depth * 1.4f + 10);
    saver_shadow_->SetOpacity(0.8f * a);
    target_->FillEllipse(D2D1::Ellipse({cx, floor}, width * 0.66f, depth * 1.4f + 10), saver_shadow_.Get());
    saver_mask_->SetStartPoint({0, floor});
    saver_mask_->SetEndPoint({0, floor + height * 0.34f});
    saver_mask_->SetOpacity(0.30f * a);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1::IdentityMatrix(), 1.0f, saver_mask_.Get()), saver_layer_.Get());
    target_->SetTransform(D2D1::Matrix3x2F::Scale(1, -1, {cx, floor}) * box * base);
    body(1.0f);
    target_->PopLayer();
    target_->SetTransform(box * base);
    body(a);
    // Light falls from the left: the sleeve darkens a little as it turns away.
    brush(color(0, 0, 0, std::clamp(0.06f - angle * 0.35f, 0.0f, 0.3f) * a));
    target_->FillRectangle(rect, solid_.Get());
    saver_sheen_->SetStartPoint({std::lerp(rect.left, rect.right, 0.26f - angle), rect.top});
    saver_sheen_->SetEndPoint({std::lerp(rect.left, rect.right, 0.74f - angle), rect.bottom});
    saver_sheen_->SetOpacity(0.09f * a);
    target_->FillRectangle(rect, saver_sheen_.Get());
}

// The cover printed on a disc turning once every 6 s: centre hole, clear hub, rim and a
// still highlight, on a dark stage.
void Renderer::drawSaverDisc(const Item& item, float alpha) {
    auto& image = saver_slots_[item.slot];
    if (!image.bitmap || !image.brush) return;
    const float a = alpha * saver_alpha_;
    const float settle = 0.97f + 0.03f * alpha;
    const float radius = viewport_.height * 0.33f * settle;
    const float cx = viewport_.width * 0.5f, cy = viewport_.height * 0.47f;
    drawSaverGlow(image.accent, cx, cy, radius * 2.1f, 0.22f * alpha);
    const D2D1_POINT_2F shadow{cx, cy + radius * 0.06f};
    saver_shadow_->SetCenter(shadow);
    saver_shadow_->SetRadiusX(radius * 1.12f);
    saver_shadow_->SetRadiusY(radius * 1.12f);
    saver_shadow_->SetOpacity(0.75f * a);
    target_->FillEllipse(D2D1::Ellipse(shadow, radius * 1.12f, radius * 1.12f), saver_shadow_.Get());
    const auto size = image.bitmap->GetSize();
    const float zoom = std::max(2 * radius / size.width, 2 * radius / size.height);
    const float degrees = 360.0f * item.age / screensaver::spin_seconds + static_cast<float>(item.seed % 360);
    image.brush->SetTransform(D2D1::Matrix3x2F::Translation(-size.width / 2, -size.height / 2) *
                              D2D1::Matrix3x2F::Scale(zoom, zoom) * D2D1::Matrix3x2F::Rotation(degrees) *
                              D2D1::Matrix3x2F::Translation(cx, cy));
    image.brush->SetOpacity(a);
    const auto disc = D2D1::Ellipse({cx, cy}, radius, radius);
    target_->FillEllipse(disc, image.brush.Get());
    // A still highlight: the light stays where it is while the print turns beneath it.
    saver_sheen_->SetStartPoint({cx - radius * 0.80f, cy - radius * 0.95f});
    saver_sheen_->SetEndPoint({cx + radius * 0.30f, cy + radius * 0.55f});
    saver_sheen_->SetOpacity(0.15f * a);
    target_->FillEllipse(disc, saver_sheen_.Get());
    saver_sheen_->SetStartPoint({cx + radius * 0.95f, cy - radius * 0.35f});
    saver_sheen_->SetEndPoint({cx - radius * 0.15f, cy + radius * 0.95f});
    saver_sheen_->SetOpacity(0.06f * a);
    target_->FillEllipse(disc, saver_sheen_.Get());
    // Clear hub with its stacking ring, then the hole.
    brush(color(0.02f, 0.022f, 0.026f, 0.84f * a));
    target_->FillEllipse(D2D1::Ellipse({cx, cy}, radius * 0.30f, radius * 0.30f), solid_.Get());
    brush(color(1, 1, 1, 0.16f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius * 0.30f, radius * 0.30f), solid_.Get(), 1.2f);
    brush(color(1, 1, 1, 0.07f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius * 0.215f, radius * 0.215f), solid_.Get(), 3.0f);
    brush(color(0.008f, 0.009f, 0.011f, a));
    target_->FillEllipse(D2D1::Ellipse({cx, cy}, radius * 0.115f, radius * 0.115f), solid_.Get());
    brush(color(1, 1, 1, 0.20f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius * 0.115f, radius * 0.115f), solid_.Get(), 1.2f);
    // Rim: a thin bright edge and a dark outline against the stage.
    brush(color(1, 1, 1, 0.06f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius * 0.965f, radius * 0.965f), solid_.Get(), 1.0f);
    brush(color(1, 1, 1, 0.30f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius - 0.8f, radius - 0.8f), solid_.Get(), 1.6f);
    brush(color(0, 0, 0, 0.5f * a));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius + 1.2f, radius + 1.2f), solid_.Get(), 1.2f);
}

void Renderer::drawSaverTitle(const std::wstring& title, float alpha) {
    const float a = alpha * saver_alpha_;
    if (title.empty() || a < 0.01f) return;
    const float y = viewport_.height - 70, width = viewport_.width * 0.55f;
    text(title, 58, y + 1.5f, width, 36, menu_font_.Get(), color(0, 0, 0, 0.6f * a));
    text(title, 57, y, width, 36, menu_font_.Get(), color(0.94f, 0.95f, 0.96f, 0.9f * a));
}
