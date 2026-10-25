#include "renderer.hpp"
#include "launch_animation.hpp"

using Microsoft::WRL::ComPtr;

// The launched game's background over the whole screen, dimmed and slowly zooming in, its logo in
// the middle (its title without one), "Uruchamianie…" beneath. Without a background: its poster or
// cover blurred, else the screen's own background darkened.
void Renderer::drawLaunchAnimation() {
    const float age = context_->launch_animation_seconds, alpha = launch_animation::opacity(age);
    if (alpha <= 0.001f) return;
    const float width = viewport_.width, height = viewport_.height;
    const auto index = context_->launch_artwork_index;
    const auto slot = [&](std::vector<Cover>& slots) -> Cover* { return index < slots.size() && slots[index].bitmap ? &slots[index] : nullptr; };
    brush(D2D1::ColorF(0, 0, 0, alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());

    ID2D1Bitmap* picture = nullptr;
    float picture_opacity = 1.0f;
    if (auto* hero = slot(heroes_)) picture = hero->bitmap.Get();
    else if (auto* art = slot(posters_) ? slot(posters_) : slot(covers_)) { picture = blurredArt(*art); picture_opacity = 0.75f; }
    if (picture) {
        // Cover-fit, then the slow zoom about the centre.
        const auto size = picture->GetSize();
        const float fit = std::max(width / std::max(1.0f, size.width), height / std::max(1.0f, size.height)) * launch_animation::zoom(age);
        const float w = size.width * fit, h = size.height * fit, x = (width - w) * 0.5f, y = (height - h) * 0.5f;
        target_->DrawBitmap(picture, D2D1::RectF(x, y, x + w, y + h), alpha * picture_opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    // A touch dimmed, darker towards the bottom where the text sits.
    brush(D2D1::ColorF(0, 0, 0, launch_animation::dim * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> shade;
    const D2D1_GRADIENT_STOP shade_stops[] = {{0, D2D1::ColorF(0, 0, 0, 0)}, {0.55f, D2D1::ColorF(0, 0, 0, 0.10f * alpha)}, {1, D2D1::ColorF(0, 0, 0, 0.55f * alpha)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, height)),
                                                    stops.Get(), shade.GetAddressOf())))
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), shade.Get());

    // The logo, centred and fitted into a box; the title in large type without one.
    const float shown = alpha * launch_animation::logo(age), scale = launch_animation::logoScale(age);
    const float cx = width * 0.5f, cy = height * 0.46f;
    float text_top = cy + 40;
    if (auto* logo = slot(logos_)) {
        const auto pixels = logo->bitmap->GetPixelSize();
        const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w > 0 && h > 0) {
            const float box_w = std::min(width * 0.46f, 760.0f), box_h = std::min(height * 0.26f, 300.0f);
            const float fit = std::min(box_w / w, box_h / h) * scale;
            const float lw = w * fit, lh = h * fit;
            target_->DrawBitmap(logo->bitmap.Get(), D2D1::RectF(cx - lw * 0.5f, cy - lh * 0.5f, cx + lw * 0.5f, cy + lh * 0.5f),
                                shown, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            text_top = cy + lh * 0.5f + 36;
        }
    } else {
        const float box_w = std::min(width * 0.8f, 1200.0f);
        textLine(context_->launch_animation_title, cx - box_w * 0.5f, cy, box_w, hero_font_.Get(),
                 D2D1::ColorF(0.97f, 0.97f, 0.97f, shown), DWRITE_TEXT_ALIGNMENT_CENTER);
        text_top = cy + typeMetrics(hero_font_.Get()).line * 0.5f + 30;
    }
    // "Uruchamianie…", its dots breathing; under it, how to hide the picture early.
    const float pulse = 0.55f + 0.25f * std::sin(age * 3.2f);
    textLine(tr(L"Uruchamianie…"), 40, text_top + 14, width - 80, settings_body_font_.Get(),
             D2D1::ColorF(0.92f, 0.92f, 0.92f, shown * pulse), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (age >= launch_animation::dismiss_after)
        textLine(tr(L"Esc / B: ukryj"), 40, height - 48, width - 80, settings_body_font_.Get(),
                 D2D1::ColorF(0.75f, 0.75f, 0.75f, 0.55f * alpha * launch_animation::ease((age - launch_animation::dismiss_after) / 0.4f)),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
}
