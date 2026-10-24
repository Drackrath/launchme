#include "renderer.hpp"
#include "controller_picture.hpp"
#include "../model/headphones.hpp"

#include <cmath>

namespace {
using palette::gray;
constexpr float pi = 3.14159265359f;
Microsoft::WRL::ComPtr<ID2D1PathGeometry> circleArc(ID2D1Factory* factory, D2D1_POINT_2F center, float radius, float start, float sweep) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    const auto point = [&](float angle) { return D2D1::Point2F(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius); };
    sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(point(start + sweep), D2D1::SizeF(radius, radius), 0,
        D2D1_SWEEP_DIRECTION_CLOCKWISE, sweep > pi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return {};
    return path;
}
D2D1_COLOR_F batteryInk(int percent, float alpha) {
    if (percent <= 20) return D2D1::ColorF(0.97f, 0.28f, 0.30f, alpha);
    if (percent <= 50) return D2D1::ColorF(1.0f, 0.72f, 0.22f, alpha);
    return D2D1::ColorF(0.30f, 0.86f, 0.50f, alpha);
}
}
void Renderer::drawControllerIcon(float x, float y, float size, D2D1_COLOR_F ink) {
    if (!controller_glyph_) return;
    D2D1_MATRIX_3X2_F previous; target_->GetTransform(&previous);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * previous);
    brush(D2D1::ColorF(ink.r, ink.g, ink.b, ink.a * 0.09f)); target_->FillGeometry(controller_glyph_.Get(), solid_.Get());
    brush(ink); target_->DrawGeometry(controller_glyph_.Get(), solid_.Get(), 1.7f / size);
    line(0.18f, 0.42f, 0.36f, 0.42f, ink, 1.6f / size);
    line(0.27f, 0.33f, 0.27f, 0.51f, ink, 1.6f / size);
    target_->FillEllipse(D2D1::Ellipse({0.73f, 0.36f}, 0.04f, 0.04f), solid_.Get());
    target_->FillEllipse(D2D1::Ellipse({0.83f, 0.46f}, 0.04f, 0.04f), solid_.Get());
    target_->SetTransform(previous);
}
void Renderer::drawNotification() {
    if (!context_->notification) { notification_cover_ = {}; return; }
    const auto& notice = *context_->notification;
    if (notification_cover_.pixels != notice.cover) {
        notification_cover_ = {}; notification_cover_.pixels = notice.cover;
        if (notice.cover) createBitmap(notification_cover_);
    }
    const float alpha = notificationOpacity(notice.age, notificationLifetime(notice.kind));
    if (alpha < 0.003f) return;
    const bool controller = notice.kind == NotificationKind::controller;
    const bool device = controller || notice.kind == NotificationKind::headphones;
    const bool error = notice.kind == NotificationKind::error;
    const bool completed = notice.kind == NotificationKind::download;
    const float width = completed ? 520.0f : device || error ? 480.0f : 412.0f;
    // Errors keep the whole reason or path: up to four lines, the toast grows to fit them.
    constexpr int error_lines = 4;
    std::wstring subtitle = notice.subtitle;
    float height = device ? 128.0f : 96.0f;
    if (error) {
        // Break opportunities after path separators, so a path wraps at folders, not mid-name.
        for (size_t at = subtitle.find_first_of(L"\\/"); at != std::wstring::npos; at = subtitle.find_first_of(L"\\/", at + 2))
            subtitle.insert(at + 1, 1, L'\u200B');
        const float text_width = width - 66 - 22;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> measure;
        DWRITE_TEXT_METRICS metrics{};
        if (!subtitle.empty() && SUCCEEDED(dwrite_->CreateTextLayout(subtitle.c_str(), static_cast<UINT32>(subtitle.size()),
                toast_sub_font_.Get(), text_width, 10000, measure.GetAddressOf())) &&
            SUCCEEDED(measure->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP)) && SUCCEEDED(measure->GetMetrics(&metrics))) {
            const int lines = std::clamp(static_cast<int>(metrics.lineCount), 1, error_lines);
            if (lines > 2) height += (lines - 2) * typeMetrics(toast_sub_font_.Get()).line;
        }
    }
    const float x = viewport_.width - width - 40;
    // Desktop mode: the bottom-right corner, clear of its top bar.
    const float y = context_->toast_bottom ? viewport_.height - height - 22 + 10 * (1 - notificationEase(notice.age / 0.23f))
                                           : (context_->window_strip ? 34 + desktop_ui::strip_height * context_->window_dpi / viewport_.scale : 34.0f) -
                                                 10 * (1 - notificationEase(notice.age / 0.23f));
    // A slightly lighter frosted panel for every toast, still without an outer frame or shadow.
    const auto panel = D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + height), 18, 18);
    const auto glass = glassPanel(panel, 18);
    glassFill(glass, D2D1::ColorF(0.14f, 0.15f, 0.17f, 0.94f), alpha);
    float text_x = x + 23;
    if (completed && notice.cover) {
        fillImage(notification_cover_, D2D1::RectF(x + 12, y + 12, x + 84, y + 84), 8, alpha, false);
        text_x = x + 104;
    } else if (device) {
        const auto center = D2D1::Point2F(x + 74, y + height * 0.5f);
        constexpr float image_radius = 50, ring_radius = 55, stroke = 3.5f;
        const auto image = D2D1::RectF(center.x - 50, center.y - 41, center.x + 50, center.y + 41);
        brush(gray(1, 0.065f * alpha));
        target_->FillEllipse(D2D1::Ellipse(center, image_radius, image_radius), solid_.Get());
        Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> mask;
        Microsoft::WRL::ComPtr<ID2D1Layer> layer;
        const bool clipped = SUCCEEDED(factory_->CreateEllipseGeometry(D2D1::Ellipse(center, image_radius, image_radius), mask.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLayer(nullptr, layer.GetAddressOf()));
        const auto photo = platform_badges_.find(controller ? L"controller:" + std::to_wstring(static_cast<int>(notice.controller_picture)) : headphones::badge);
        if (clipped) {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(), alpha), layer.Get());
            bool painted = false;
            if (photo != platform_badges_.end() && photo->second.bitmap) {
                const auto size = photo->second.bitmap->GetSize();
                const auto source = controllerPictureSource(controller ? notice.controller_picture : devices::Picture::none, size,
                    photo->second.has_ink_bounds ? photo->second.ink_bounds : D2D1::RectF(0, 0, size.width, size.height));
                const float source_w = source.right - source.left, source_h = source.bottom - source.top;
                if (source_w > 0 && source_h > 0) {
                    const float factor = std::min((image.right - image.left) / source_w, (image.bottom - image.top) / source_h);
                    const float w = source_w * factor, h = source_h * factor;
                    target_->DrawBitmap(photo->second.bitmap.Get(), D2D1::RectF(center.x - w / 2, center.y - h / 2, center.x + w / 2, center.y + h / 2),
                                        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
                    painted = true;
                }
            }
            if (!painted && controller) drawControllerArt(notice.controller_picture, image);
            else if (!painted) {
                // A neutral headset glyph while the photo is arriving.
                const auto band = circleArc(factory_.Get(), center, 24, pi, pi);
                brush(gray(0.91f));
                if (band) target_->DrawGeometry(band.Get(), solid_.Get(), 3);
                for (const float side : {-1.0f, 1.0f}) {
                    const float cx = center.x + side * 23;
                    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 5, center.y - 2, cx + 5, center.y + 22), 4, 4), solid_.Get(), 3);
                }
            }
            target_->PopLayer();
        }
        const bool known = notice.connected && notice.battery && *notice.battery >= 0 && *notice.battery <= 100;
        const int percent = known ? *notice.battery : 0;
        brush(known ? batteryInk(percent, 0.23f * alpha) : gray(0.90f, 0.18f * alpha));
        target_->DrawEllipse(D2D1::Ellipse(center, ring_radius, ring_radius), solid_.Get(), stroke);
        if (known && percent > 0) {
            brush(batteryInk(percent, alpha));
            if (percent == 100) target_->DrawEllipse(D2D1::Ellipse(center, ring_radius, ring_radius), solid_.Get(), stroke);
            else {
                const float sweep = 2 * pi * static_cast<float>(percent) / 100;
                const auto arc = circleArc(factory_.Get(), center, ring_radius, -pi * 0.5f, sweep);
                if (arc) {
                    target_->DrawGeometry(arc.Get(), solid_.Get(), stroke);
                    for (const float angle : {-pi * 0.5f, -pi * 0.5f + sweep})
                        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x + std::cos(angle) * ring_radius,
                            center.y + std::sin(angle) * ring_radius), stroke * 0.5f, stroke * 0.5f), solid_.Get());
                }
            }
        }
        text_x = x + 148;
    } else if (error) {
        // A red badge with an exclamation mark, on the toast's centre line.
        const auto center = D2D1::Point2F(x + 38, y + height * 0.5f);
        brush(palette::dangerStrong(alpha));
        target_->FillEllipse(D2D1::Ellipse(center, 15, 15), solid_.Get());
        line(center.x, center.y - 7.5f, center.x, center.y + 2.5f, gray(1, alpha), 3);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x, center.y + 7.5f), 1.9f, 1.9f), solid_.Get());
        text_x = x + 66;
    }
    // Title and subtitle as one group on the toast's centre line.
    textGroup(notice.title, subtitle, text_x, y + height * 0.5f, x + width - text_x - 22, toast_font_.Get(), gray(0.97f, alpha),
              toast_sub_font_.Get(), gray(0.76f, alpha), device ? 1 : error ? error_lines : 2);
}
void Renderer::drawVolumeIndicator() {
    if (!context_->volume_notice) return;
    const auto& notice = *context_->volume_notice;
    const float alpha = notificationEase(notice.age / 0.16f) * notificationEase((1.8f - notice.age) / 0.30f);
    if (alpha < 0.003f) return;
    const float width = std::min(440.0f, viewport_.width - 230), x = (viewport_.width - width) * 0.5f, y = viewport_.height - 25;
    const auto track = D2D1::RoundedRect(D2D1::RectF(x, y - 2, x + width, y + 2), 2, 2);
    brush(gray(0, 0.40f * alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 2, y - 4, x + width + 2, y + 4), 4, 4), solid_.Get());
    brush(gray(1, 0.2f * alpha)); target_->FillRoundedRectangle(track, solid_.Get());
    const float end = x + width * std::clamp(notice.level, 0.0f, 1.0f);
    if (end > x && !notice.muted) {
        brush(gray(0.97f, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y - 2, end, y + 2), 2, 2), solid_.Get());
    }
    drawAudioIcon(x - 28, y, notice.input, notice.muted, gray(0.88f, alpha));
    textLine(notice.muted ? L"—" : std::to_wstring(static_cast<int>(notice.level * 100 + 0.5f)), x + width + 12, y, 42,
             button_font_.Get(), gray(0.84f, alpha));
}
