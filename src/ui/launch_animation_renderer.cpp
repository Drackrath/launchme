#include "renderer.hpp"
#include "launch_animation.hpp"

void Renderer::drawLaunchAnimation() {
    const float age = context_->launch_animation_seconds, alpha = launch_animation::opacity(age);
    const auto index = context_->launch_artwork_index;
    Cover* image = index < posters_.size() && posters_[index].bitmap ? &posters_[index] :
                   index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    float ratio = 1;
    if (image) {
        const auto size = image->bitmap->GetSize();
        if (size.height > 0) ratio = std::clamp(size.width / size.height, 0.5f, 2.0f);
    }
    const float scale = launch_animation::scale(age);
    const float height = std::min(viewport_.height * 0.52f, viewport_.width * 0.62f / ratio) * scale;
    const float width = height * ratio, cx = viewport_.width * 0.5f, cy = viewport_.height * 0.44f;
    const auto rect = D2D1::RectF(cx - width * 0.5f, cy - height * 0.5f, cx + width * 0.5f, cy + height * 0.5f);
    brush(D2D1::ColorF(0, 0, 0, 0.62f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const float corner = squircle_corner_, reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 100;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = corner; corner_reference_ = reference;
    if (!shape) return;
    brush(D2D1::ColorF(0.10f, 0.12f, 0.15f, alpha));
    target_->FillGeometry(shape.Get(), solid_.Get());
    if (image) fillImage(*image, rect, 0, alpha, false, shape.Get());
    else textLine(context_->launch_animation_title, rect.left + 20, cy, width - 40,
                  category_font_.Get(), D2D1::ColorF(1, 1, 1, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    brush(D2D1::ColorF(1, 1, 1, alpha * 0.65f));
    target_->DrawGeometry(shape.Get(), solid_.Get(), 1.5f);
    textLine(context_->launch_animation_title, 40, rect.bottom + 38, viewport_.width - 80,
             category_font_.Get(), D2D1::ColorF(0.96f, 0.96f, 0.96f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(tr(L"Uruchamianie…"), 40, rect.bottom + 72, viewport_.width - 80,
             settings_body_font_.Get(), D2D1::ColorF(0.65f, 0.65f, 0.65f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
}
