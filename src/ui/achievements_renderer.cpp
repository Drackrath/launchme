// Szczegóły → Osiągnięcia over the details screen (src/app/achievements.cpp).
#include "renderer.hpp"

#include <algorithm>

namespace {
using palette::gray;
D2D1_RECT_F box(settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
}

void Renderer::drawAchievements() {
    const auto& state = *context_->achievements;
    const float width = viewport_.width, height = viewport_.height;
    const achievements_ui::Layout layout{width, height};
    brush(D2D1::ColorF(0.018f, 0.021f, 0.027f, 1.0f)); // Covers the details beneath completely.
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    const auto back = layout.back();
    // Back: a glass circle with a dot, white while it has the focus (Up from the first row).
    const auto circle = backCircle(back.left, back.top, back.right, back.bottom);
    brush(drawButton(circle, state.selected < 0 ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((circle.left + circle.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
    const float left = layout.left(), right = layout.right();
    textLine(tr(L"Osiągnięcia"), back.right + 22, (back.top + back.bottom) * 0.5f, right - back.right - 22, settings_heading_font_.Get(), gray(0.96f));
    // Game, count and progress.
    const float header = layout.panel().top();
    textLine(state.source.empty() ? state.title : state.title + L" · " + state.source, left, header + 14, right - left - 260, menu_font_.Get(), gray(0.7f));
    if (state.total) {
        const std::wstring count = std::to_wstring(state.unlocked) + L" / " + std::to_wstring(state.total) +
                                   L"  (" + std::to_wstring(state.unlocked * 100 / state.total) + L"%)";
        textLine(count, right - 250, header + 14, 250, menu_font_.Get(), gray(0.95f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float share = static_cast<float>(state.unlocked) / state.total;
        brush(gray(1, 0.12f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, header + 44, right, header + 50), 3, 3), solid_.Get());
        if (share > 0) {
            brush(uiAccent());
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, header + 44, left + (right - left) * share, header + 50), 3, 3), solid_.Get());
        }
    }
    if (state.items.empty()) {
        textLine(state.loading ? std::wstring(tr(L"Wczytywanie…")) : !state.message.empty() ? state.message : std::wstring(tr(L"Lista osiągnięć nie jest dostępna.")),
                 left, layout.top() + 30, right - left, menu_font_.Get(), gray(0.6f));
        return;
    }
    target_->PushAxisAlignedClip(D2D1::RectF(left - 6, layout.top() - 6, right + 6, layout.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < state.items.size(); ++i) {
        const auto r = layout.row(i, state.scroll);
        if (r.bottom < layout.top() - 6 || r.top > layout.bottom()) continue;
        const auto& item = state.items[i];
        const bool focused = static_cast<int>(i) == state.selected;
        if (focused) { // A translucent highlight, no outline.
            brush(gray(1, 0.12f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 16, 16), solid_.Get());
        }
        const float icon_top = r.top + (layout.row_height - layout.icon) * 0.5f;
        const auto icon = D2D1::RectF(r.left + 12, icon_top, r.left + 12 + layout.icon, icon_top + layout.icon);
        brush(gray(1, 0.06f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(icon, 10, 10), solid_.Get());
        if (i < artwork_thumbnails_.size() && artwork_thumbnails_[i].bitmap)
            fillImage(artwork_thumbnails_[i], icon, 10, (item.achieved ? 1.0f : 0.6f) * reveal(artwork_thumbnails_[i])); // Locked: the grey icon.
        const float text_left = icon.right + 20, detail_width = 230;
        textGroup(item.name, item.description, text_left, (r.top + r.bottom) * 0.5f, r.right - text_left - detail_width - 24,
                  menu_font_.Get(), gray(item.achieved ? 0.97f : 0.7f), settings_body_font_.Get(), gray(item.achieved ? 0.62f : 0.46f), 2);
        if (!item.detail.empty())
            textLine(item.detail, r.right - detail_width - 18, (r.top + r.bottom) * 0.5f, detail_width, settings_body_font_.Get(),
                     item.achieved ? uiAccent() : gray(0.52f), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    // The client's cache holds only part of the list: say how many are not shown.
    if (!state.complete && state.total > state.items.size()) {
        const auto r = layout.row(state.items.size(), state.scroll);
        if (r.top < layout.bottom())
            textLine(tr(L"Pozostałe osiągnięcia: ") + std::to_wstring(state.total - state.items.size()), r.left + 12,
                     r.top + layout.row_height * 0.5f, r.right - r.left - 24, settings_body_font_.Get(), gray(0.5f));
    }
    target_->PopAxisAlignedClip();
    const float maximum = layout.maxScroll(state.rows());
    if (maximum > 0) {
        const float length = layout.bottom() - layout.top();
        const float thumb = std::max(30.0f, length * length / (length + maximum));
        const float y = layout.top() + (length - thumb) * std::min(1.0f, state.scroll / maximum);
        brush(gray(1, 0.3f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(right + 14, y, right + 17, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
}
