// Launchfeed's iPhone: a titanium rim, Dynamic Island, native-looking social cards, continuous
// scrolling and a home indicator. The whole chat disappears; only the active profile is shown.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "../net/companion_chats.hpp"

using Microsoft::WRL::ComPtr;
namespace {
using render_detail::box;
}

void Renderer::drawCompanionFeed() {
    const auto* view = context_->companion;
    if (!view || !view->feed) return;
    const auto& feed = *view->feed;
    using L = companion_ui::FeedLayout;
    const L layout{viewport_.width, viewport_.height};
    using palette::gray;
    const auto tint = companion_ui::tint(view->look);
    const D2D1_COLOR_F accent = D2D1::ColorF(tint.r, tint.g, tint.b);
    const auto round = [&](D2D1_RECT_F r, float radius, D2D1_COLOR_F color) {
        brush(color); target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), solid_.Get());
    };
    brush(D2D1::ColorF(0.018f, 0.021f, 0.028f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    // A subtle pool of her profile colour behind the device.
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1RadialGradientBrush> glow;
    D2D1_GRADIENT_STOP colors[]{{0, D2D1::ColorF(tint.r, tint.g, tint.b, 0.12f)}, {1, gray(0, 0)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(viewport_.width / 2, viewport_.height / 2), {},
            viewport_.height * 0.55f, viewport_.height * 0.6f), stops.Get(), glow.GetAddressOf())))
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), glow.Get());

    if (!feed_body_font_) {
        dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17, L"", feed_body_font_.GetAddressOf());
        dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"", feed_small_font_.GetAddressOf());
        dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26, L"", feed_title_font_.GetAddressOf());
    }
    if (!feed_body_font_ || !feed_small_font_ || !feed_title_font_) return;
    D2D1_MATRIX_3X2_F saved{}; target_->GetTransform(&saved);
    const float appearance = feed.entrance.value;
    const float scale = layout.scale() * (0.94f + 0.06f * std::clamp(appearance, 0.0f, 1.0f));
    const float slide = (1 - appearance) * (viewport_.height + 120);
    target_->SetTransform(D2D1::Matrix3x2F::Rotation(-5.0f * (1 - appearance), D2D1::Point2F(220, 440)) * D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation((viewport_.width - L::phone_w * scale) * 0.5f, layout.upper() + slide) * D2D1::Matrix3x2F::ReinterpretBaseType(&saved)[0]);
    for (int i = 12; i > 0; --i)
        round(D2D1::RectF(-i * 1.5f, static_cast<float>(i), L::phone_w + i * 1.5f, L::phone_h + i * 2.5f), 58.0f + i, gray(0, 0.025f));
    // Brushed metal edges, buttons and the inset black glass.
    round(D2D1::RectF(-3, 160, 3, 202), 3, gray(0.35f));
    round(D2D1::RectF(-3, 220, 3, 276), 3, gray(0.35f));
    round(D2D1::RectF(437, 218, 443, 302), 3, gray(0.35f));
    round(D2D1::RectF(0, 0, 440, 880), 56, gray(0.39f));
    brush(gray(0.74f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(1.5f, 1.5f, 438.5f, 878.5f), 55, 55), solid_.Get(), 1);
    round(D2D1::RectF(4, 4, 436, 876), 52, gray(0.025f));
    round(D2D1::RectF(12, 12, 428, 868), 46, D2D1::ColorF(0.055f, 0.06f, 0.073f));
    SYSTEMTIME now{}; GetLocalTime(&now);
    const auto two = [](int n) { return (n < 10 ? L"0" : L"") + std::to_wstring(n); };
    textLine(two(now.wHour) + L":" + two(now.wMinute), 37, 40, 75, feed_body_font_.Get(), gray(0.94f), DWRITE_TEXT_ALIGNMENT_CENTER);
    round(D2D1::RectF(156, 24, 284, 57), 17, gray(0.005f));
    brush(D2D1::ColorF(0.08f, 0.12f, 0.18f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(264, 40), 4, 4), solid_.Get());
    for (int i = 0; i < 4; ++i) round(D2D1::RectF(333.0f + i * 5, 45.0f - i * 3, 336.0f + i * 5, 48), 1, gray(0.88f));
    brush(gray(0.7f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(367, 34, 394, 47), 3, 3), solid_.Get(), 1);
    round(D2D1::RectF(370, 37, 390, 44), 1, gray(0.9f));
    round(D2D1::RectF(395, 38, 397, 43), 1, gray(0.65f));
    // Two seconds of app launch, independent from the network request. Once booted, loading
    // or offline states belong to the feed itself rather than an endless splash screen.
    if (feed.app_age < 2) {
        round(D2D1::RectF(164, 306, 276, 418), 29, D2D1::ColorF(tint.r, tint.g, tint.b, 0.16f));
        round(D2D1::RectF(188, 333, 252, 345), 6, accent);
        round(D2D1::RectF(188, 358, 239, 370), 6, accent);
        round(D2D1::RectF(188, 383, 224, 395), 6, accent);
        textLine(L"Launchfeed", 40, 460, 360, feed_title_font_.Get(), gray(0.96f), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(view->name, 40, 495, 360, feed_body_font_.Get(), gray(0.55f), DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 0.785398f;
            const int lit = static_cast<int>(feed.app_age * 8) % 8;
            brush(gray(0.9f, i == lit ? 0.9f : 0.18f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(220 + 13 * std::cos(angle), 557 + 13 * std::sin(angle)), 2.5f, 2.5f), solid_.Get());
        }
        round(D2D1::RectF(155, 854, 285, 859), 3, gray(0.78f));
        line(390, 84, 402, 96, gray(0.55f), 1.8f); line(390, 96, 402, 84, gray(0.55f), 1.8f);
        target_->SetTransform(saved); return;
    }
    textLine(L"Launchfeed", 30, 91, 310, feed_title_font_.Get(), gray(0.98f));
    const auto close = L::close();
    round(box(close), 24, gray(1, feed.hover == L::close_hit ? 0.14f : 0.06f));
    line(390, 84, 402, 96, gray(0.82f), 1.8f); line(390, 96, 402, 84, gray(0.82f), 1.8f);
    round(D2D1::RectF(30, 123, 38, 131), 4, accent);
    textLine(view->name + L"  ·  " + tr(L"Tylko ten profil"), 47, 127, 350, feed_small_font_.Get(), gray(0.64f));
    line(26, 151, 414, 151, gray(1, 0.08f), 1);

    std::erase_if(companion_feed_images_, [&](const auto& item) { return !feed.images.contains(item.first); });
    const auto [first, last] = L::visible(feed.archive, feed.motion.value, feed.thread);
    target_->PushAxisAlignedClip(box(L::content(feed.thread)), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = first; i < last; ++i) {
        const auto& post = feed.archive.posts[i];
        const auto card = L::card(feed.archive, i, feed.motion.value, feed.thread);
        round(box(card), 19, D2D1::ColorF(0.085f, 0.092f, 0.108f));
        brush(gray(1, 0.065f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(card), 19, 19), solid_.Get(), 1);
        const float y = card.top;
        round(D2D1::RectF(38, y + 12, 74, y + 48), 18, D2D1::ColorF(tint.r, tint.g, tint.b, 0.18f));
        textLine(view->name.substr(0, 1), 38, y + 30, 36, feed_body_font_.Get(), accent, DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(view->name, 86, y + 23, 290, feed_body_font_.Get(), gray(0.96f));
        textLine(companion::chats::label(post.at, i18n::english()), 86, y + 43, 290, feed_small_font_.Get(), gray(0.52f));
        const auto photo = L::photo(feed.archive, i, feed.motion.value, feed.thread);
        round(box(photo), 0, gray(0.035f));
        const auto bytes = feed.images.find(post.id);
        auto& image = companion_feed_images_[post.id];
        if (bytes != feed.images.end() && image.pixels != bytes->second) { image = {}; image.pixels = bytes->second; image.short_edge = 960; }
        if (image.pixels && !image.pixels->empty()) createBitmap(image);
        if (image.bitmap) {
            // Contain, never crop away part of the authored photo.
            const auto size = image.bitmap->GetSize();
            const float fit = std::min((photo.right - photo.left) / std::max(size.width, 1.0f), (photo.bottom - photo.top) / std::max(size.height, 1.0f));
            const float w = size.width * fit, h = size.height * fit;
            const float x = (photo.left + photo.right - w) * 0.5f, py = (photo.top + photo.bottom - h) * 0.5f;
            target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, py, x + w, py + h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else textLine(bytes != feed.images.end() && bytes->second->empty() ? tr(L"Zdjęcie niedostępne") : tr(L"Wczytywanie zdjęcia…"),
                        photo.left + 20, (photo.top + photo.bottom) / 2, photo.right - photo.left - 40, feed_small_font_.Get(), gray(0.5f), DWRITE_TEXT_ALIGNMENT_CENTER);
        // A readable native text layout (not text painted into the picture).
        ComPtr<IDWriteTextLayout> caption;
        const float caption_height = L::captionHeight(post);
        if (SUCCEEDED(dwrite_->CreateTextLayout(post.text.c_str(), static_cast<UINT32>(post.text.size()), feed_body_font_.Get(), 356, caption_height, caption.GetAddressOf()))) {
            caption->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            caption->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DWRITE_TEXT_METRICS metrics{};
            for (float font = 16; font >= 10 && SUCCEEDED(caption->GetMetrics(&metrics)) && metrics.height > caption_height; --font)
                caption->SetFontSize(font, {0, static_cast<UINT32>(post.text.size())});
            brush(gray(0.93f)); target_->DrawTextLayout(D2D1::Point2F(42, photo.bottom + 17), caption.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        for (int action = 0; action < 3; ++action) {
            const auto r = L::action(feed.archive, i, action, feed.motion.value, feed.thread);
            const bool hovered = feed.hover == i * 3 + action;
            const bool focused = feed.thread.empty() && action == feed.focus && i == L::visible(feed.archive, feed.motion.value + 160, feed.thread).first;
            round(box(r), 12, gray(1, hovered ? 0.1f : focused ? 0.065f : 0.025f));
            if (focused) { brush(gray(1, 0.22f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), 12, 12), solid_.Get(), 1); }
            const float x = r.left + 24, cy = (r.top + r.bottom) * 0.5f;
            const D2D1_COLOR_F ink = action == 0 && post.social.liked ? D2D1::ColorF(1, 0.32f, 0.47f) : action == 2 && !feed.can_discuss ? gray(0.38f) : gray(0.78f);
            if (action == 0) {
                const float pulse = feed.heart == post.id && feed.heart_age < 0.6f ? 1 + 0.22f * std::sin(std::min(feed.heart_age / 0.6f, 1.0f) * 3.141593f) : 1;
                ComPtr<ID2D1PathGeometry> heart; ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(factory_->CreatePathGeometry(&heart)) && SUCCEEDED(heart->Open(&sink))) {
                    const auto pt = [&](float dx, float dy) { return D2D1::Point2F(x + dx * pulse, cy + dy * pulse); };
                    sink->BeginFigure(pt(0, 9), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddBezier(D2D1::BezierSegment(pt(-21, -3), pt(-8, -17), pt(0, -6)));
                    sink->AddBezier(D2D1::BezierSegment(pt(8, -17), pt(21, -3), pt(0, 9)));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
                    brush(ink); if (post.social.liked) target_->FillGeometry(heart.Get(), solid_.Get()); else target_->DrawGeometry(heart.Get(), solid_.Get(), 1.7f);
                }
            } else if (action == 1) {
                brush(ink); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 10, cy - 9, x + 10, cy + 6), 5, 5), solid_.Get(), 1.6f);
                line(x - 6, cy + 6, x - 7, cy + 11, ink, 1.6f); line(x - 7, cy + 11, x, cy + 6, ink, 1.6f);
            } else {
                line(x - 11, cy - 7, x + 12, cy - 12, ink, 1.7f); line(x + 12, cy - 12, x + 5, cy + 12, ink, 1.7f);
                line(x + 5, cy + 12, x - 1, cy + 1, ink, 1.7f); line(x - 1, cy + 1, x - 11, cy - 7, ink, 1.7f); line(x - 1, cy + 1, x + 12, cy - 12, ink, 1.7f);
            }
            const auto label = action == 0 ? std::wstring(post.social.liked ? tr(L"Lubisz") : tr(L"Polub")) : action == 1 ? std::to_wstring(post.social.comments.size()) : std::wstring(L"Priv");
            textLine(label, r.left + 45, cy, r.right - r.left - 53, feed_small_font_.Get(), ink);
        }
        if (feed.thread == post.id) {
            float cy = y + L::actionsTop(post) + 66;
            textLine(tr(L"Komentarze"), 42, cy, 350, feed_body_font_.Get(), gray(0.73f)); cy += 26;
            for (const auto& comment : post.social.comments) {
                textLine(comment.player ? tr(L"Ty") : comment.author, 42, cy, 356, feed_small_font_.Get(), comment.player ? accent : gray(0.62f));
                ComPtr<IDWriteTextLayout> text;
                const float h = L::textHeight(comment.text, 36);
                if (SUCCEEDED(dwrite_->CreateTextLayout(comment.text.c_str(), static_cast<UINT32>(comment.text.size()), feed_body_font_.Get(), 350, h, &text))) {
                    text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    DWRITE_TEXT_METRICS metrics{};
                    for (float font = 16; font >= 10 && SUCCEEDED(text->GetMetrics(&metrics)) && metrics.height > h; --font)
                        text->SetFontSize(font, {0, static_cast<UINT32>(comment.text.size())});
                    brush(gray(0.9f)); target_->DrawTextLayout(D2D1::Point2F(42, cy + 15), text.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                cy += L::commentHeight(comment);
            }
        }
    }
    if (feed.archive.posts.empty()) {
        textLine(feed.busy ? tr(L"Przygotowuje pierwszy post…") : tr(L"Jeszcze tu cicho"), 38, 370, 364, feed_title_font_.Get(), gray(0.88f), DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto message = !feed.status.empty() ? feed.status : !feed.configured ? tr(L"Ustawienia → Zdjęcia → Folder feedu") : tr(L"Zdjęcie i opis TXT zamienią się w jej post.");
        ComPtr<IDWriteTextLayout> text;
        if (SUCCEEDED(dwrite_->CreateTextLayout(message.c_str(), static_cast<UINT32>(message.size()), feed_body_font_.Get(), 332, 180, text.GetAddressOf()))) {
            text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            brush(gray(0.55f)); target_->DrawTextLayout(D2D1::Point2F(54, 414), text.Get(), solid_.Get());
        }
    }
    target_->PopAxisAlignedClip();
    // Subtle progress rail; content stays clipped inside the phone's display.
    const float limit = L::limit(feed.archive, feed.thread);
    if (limit > 0) {
        const float track = L::content(feed.thread).bottom - L::top;
        const float length = std::max(24.0f, track * track / (limit + track));
        const float y = L::top + (track - length) * std::clamp(feed.motion.value / limit, 0.0f, 1.0f);
        round(D2D1::RectF(420, y, 423, y + length), 2, gray(0.7f, 0.4f));
    }
    if (!feed.thread.empty()) {
        round(D2D1::RectF(16, 716, 424, 818), 0, D2D1::ColorF(0.055f, 0.06f, 0.073f));
        textLine(tr(L"‹ Wróć do postów"), 30, 731, 380, feed_small_font_.Get(), gray(0.64f));
        const auto field = L::input(), submit = L::send();
        round(box(field), 15, gray(1, 0.055f));
        if (feed.focus == 3) { brush(D2D1::ColorF(tint.r, tint.g, tint.b, 0.4f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(field), 15, 15), solid_.Get(), 1); }
        const auto value = feed.input.empty() ? std::wstring(tr(L"Dodaj komentarz…")) : feed.input;
        std::wstring shown = value;
        while (shown.size() > 1 && textAdvance(shown, feed_body_font_.Get()) > 290) shown.erase(shown.begin());
        textLine(shown, field.left + 14, 778, 294, feed_body_font_.Get(), feed.input.empty() ? gray(0.4f) : gray(0.94f));
        if (feed.focus == 3 && std::fmod(elapsed_, 1.0f) < 0.6f) {
            const float x = field.left + 14 + (feed.input.empty() ? 0 : textAdvance(shown, feed_body_font_.Get()));
            line(x, 766, x, 788, accent, 1.2f);
        }
        round(box(submit), 15, D2D1::ColorF(tint.r, tint.g, tint.b, feed.focus == 4 || feed.hover == L::send_hit ? 0.25f : 0.13f));
        textLine(feed.social_busy ? L"…" : L"↑", submit.left, 778, submit.right - submit.left, feed_title_font_.Get(), accent, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!feed.archive.posts.empty()) textLine(feed.busy ? tr(L"Pisze nowy post…") : !feed.status.empty() ? feed.status : L"Launchfeed · " + view->name,
        34, 838, 372, feed_small_font_.Get(), gray(0.44f), DWRITE_TEXT_ALIGNMENT_CENTER);
    round(D2D1::RectF(155, 854, 285, 859), 3, gray(0.78f));
    target_->SetTransform(saved);
}
