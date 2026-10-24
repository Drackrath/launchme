#include "renderer.hpp"
#include "friends_layout.hpp"
#include <d2d1helper.h>

void Renderer::drawPluginPage(float origin, bool drawer) {
    const auto* content = drawer ? context_->plugin_drawer : context_->plugin_page;
    if (!content) return;
    const auto& page = *content;
    const float width = drawer ? friends_layout::drawer_width : viewport_.width;
    const plugin_ui::Layout layout{width, viewport_.height, drawer, page.dialog};
    if (layout.dialog) {
        brush(palette::gray(0, 0.65f)); target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), solid_.Get());
        brush(palette::gray(0.075f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(layout.left - 32, layout.panelTop(), width - layout.left + 32, layout.bottom() + 24), 24, 24), solid_.Get());
    }
    const auto white = D2D1::ColorF(0.95f, 0.95f, 0.95f);
    const float back_size = drawer ? 36.0f : 48.0f;
    const auto back = D2D1::RectF(origin + layout.backLeft(), layout.backTop(), origin + layout.backLeft() + back_size, layout.backTop() + back_size);
    // Back: a round glass button with a dot (the drawer is frosted already), white with the focus.
    const auto back_ink = drawButton(back, page.focus < 0 ? 1.0f : 0.0f, 1, false);
    brush(back_ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3, 3), solid_.Get());
    textLine(page.title, back.right + 14, (back.top + back.bottom) * 0.5f, origin + width - layout.left - back.right - 14, category_font_.Get(), white);
    text(page.status, origin + layout.left, layout.dialog ? layout.panelTop() + 94 : drawer ? 82.0f : 122.0f, width - 2 * layout.left, 28, toast_sub_font_.Get(), D2D1::ColorF(0.65f, 0.65f, 0.65f));
    target_->PushAxisAlignedClip(D2D1::RectF(origin + layout.left - 8, layout.top, origin + width - layout.left + 8, layout.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    auto& images = drawer ? plugin_thumbnails_ : covers_;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const auto& row = page.rows[i]; const float y = layout.rowTop(page, i);
        if (y + row.height() < layout.top || y > layout.bottom()) continue;
        if (row.heading) {
            text(row.title, origin + layout.left + 6, y + 10, width - 2 * layout.left - 12, 36,
                 drawer ? friend_section_font_.Get() : friend_name_font_.Get(), D2D1::ColorF(0.66f, 0.75f, 0.73f)); continue;
        }
        if (row.input) {
            // The search field: a pill, brighter and outlined in the accent while it takes typing.
            const bool typing = page.focus == static_cast<int>(i);
            const auto field = D2D1::RectF(origin + layout.left - 6, y + 4, origin + width - layout.left + 6, y + row.height() - 10);
            brush(palette::gray(1, typing ? 0.13f : 0.06f)); target_->FillRoundedRectangle(pill(field), solid_.Get());
            if (typing) { brush(uiAccent()); target_->DrawRoundedRectangle(pill(field), solid_.Get(), 1.5f); }
            const float middle = (field.top + field.bottom) * 0.5f;
            drawSearchGlyph(field.left + 30, middle, 8, palette::gray(0.9f), 1.8f);
            const float left = field.left + 54, room = field.right - left - 24;
            // The end of a long query stays in view, as in the other search fields.
            const auto shown = page.query.size() > 60 ? L"…" + page.query.substr(page.query.size() - 59) : page.query;
            const float advance = shown.empty() ? 0.0f : std::min(room - 8, textAdvance(shown, menu_font_.Get()));
            if (shown.empty()) textLine(row.title, left + (typing ? 10.0f : 0.0f), middle, room - 10, menu_font_.Get(), palette::gray(typing ? 0.45f : 0.55f));
            else textLine(shown, left, middle, room, menu_font_.Get(), white);
            if (typing && std::sin(elapsed_ * 4.0f) >= 0) textLine(L"│", left + advance - 2, middle, 20, menu_font_.Get(), white);
            continue;
        }
        if (page.focus == static_cast<int>(i)) {
            // A translucent highlight: a pill on short rows, 16 px corners on taller ones.
            brush(palette::gray(1, 0.10f));
            const auto box = D2D1::RectF(origin + layout.left - 6, y, origin + width - layout.left + 6, y + row.height() - 6);
            target_->FillRoundedRectangle(box.bottom - box.top <= 64 ? pill(box) : D2D1::RoundedRect(box, 16, 16), solid_.Get());
        }
        float x = origin + layout.left + 6;
        if (!row.game_key.empty() || !row.download_id.empty()) {
            const float size = drawer ? 52.0f : !row.download_id.empty() ? 104.0f : 70.0f, top = y + (row.height() - size) * 0.5f - 3;
            const auto box = D2D1::RectF(x, top, x + size, top + size);
            const float radius = size * 0.1f;
            brush(D2D1::ColorF(0.12f, 0.15f, 0.18f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), solid_.Get());
            if (i < images.size()) fillImage(images[i], box, radius, reveal(images[i]), false);
            x += size + 18;
        }
        textGroup(row.title, row.hasSubtitle() ? row.subtitle : std::wstring{}, x, y + (row.height() - 6) * 0.5f,
                  origin + width - layout.left - 8 - x, drawer ? friend_name_font_.Get() : menu_font_.Get(), row.enabled ? white : D2D1::ColorF(0.45f, 0.45f, 0.45f),
                  friend_status_font_.Get(), D2D1::ColorF(0.6f, 0.6f, 0.6f), 1);
    }
    target_->PopAxisAlignedClip();
    const auto maximum = layout.maxScroll(page);
    if (maximum > 0) {
        const float track = layout.bottom() - layout.top, thumb = std::max(24.0f, track * track / (maximum + track));
        const float y = layout.top + (track - thumb) * std::clamp(page.scroll / maximum, 0.0f, 1.0f);
        brush(D2D1::ColorF(1, 1, 1, 0.25f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(origin + width - 7, y, origin + width - 4, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
}
