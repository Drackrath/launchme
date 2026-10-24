#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace {
D2D1_RECT_F box(settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
using palette::gray;
// How far the gliding focus frame has reached a button (1 without the frame): the button's white
// pill fades in as the frame arrives (as in device_renderer.cpp).
float arrival(const std::optional<ViewContext::GlideBox>& glide, settings_ui::Rect r) {
    if (!glide) return 1;
    const float off = std::abs(glide->left - r.left) + std::abs(glide->top - r.top) + std::abs(glide->right - r.right) +
                      std::abs(glide->bottom - r.bottom);
    return std::clamp(1 - off / (2 * (r.bottom - r.top)), 0.0f, 1.0f);
}
}

void Renderer::drawAudioIcon(float x, float y, bool input, bool muted, D2D1_COLOR_F ink) {
    brush(ink);
    if (input) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 4, y - 10, x + 4, y + 3), 4, 4), solid_.Get(), 1.6f);
        target_->PushAxisAlignedClip(D2D1::RectF(x - 10, y - 1, x + 10, y + 10), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 8, y - 8, x + 8, y + 7), 7, 7), solid_.Get(), 1.6f);
        target_->PopAxisAlignedClip();
        line(x, y + 7, x, y + 11, ink, 1.6f); line(x - 4, y + 12, x + 4, y + 12, ink, 1.6f);
    } else {
        line(x - 9, y - 4, x - 5, y - 4, ink, 1.6f);
        line(x - 9, y - 4, x - 9, y + 4, ink, 1.6f);
        line(x - 9, y + 4, x - 5, y + 4, ink, 1.6f);
        line(x - 5, y - 4, x + 1, y - 9, ink, 1.6f);
        line(x - 5, y + 4, x + 1, y + 9, ink, 1.6f);
        line(x + 1, y - 9, x + 1, y + 9, ink, 1.6f);
        if (!muted) {
            target_->PushAxisAlignedClip(D2D1::RectF(x + 5, y - 12, x + 14, y + 12), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + 1, y), 7, 8), solid_.Get(), 1.4f);
            target_->PopAxisAlignedClip();
        }
    }
    if (muted) line(x - 11, y - 12, x + 11, y + 12, ink, 1.8f);
}

void Renderer::drawAudioDevices() {
    const auto& state = *context_->device_panel;
    const device_ui::AudioLayout layout{viewport_.width, viewport_.height};
    const auto fill = [&](settings_ui::Rect r, D2D1_COLOR_F ink, float radius = 16) {
        brush(ink); target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](settings_ui::Rect r, D2D1_COLOR_F ink, float thickness = 1.0f) {
        brush(ink); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), 16, 16), solid_.Get(), thickness);
    };
    const bool frame = context_->device_glide.has_value();
    for (int column = 0; column < 2; ++column) {
        const auto card = layout.card(column), track = layout.track(column), mute = layout.mute(column);
        const auto& channel = state.data.audio[column];
        const bool selected_column = state.selected >= 0 && !state.audioFooter(state.selected) && state.rows[state.selected].input == (column != 0);
        const bool slider_focus = selected_column && state.rows[state.selected].kind == device_ui::Kind::volume;
        const bool editing = slider_focus && state.audio_edit_column == column;
        // With the gliding focus frame (drawDeviceFocus, under the items) the items keep their resting
        // look and the mute button turns white as the frame reaches it; without the frame the
        // focused item has a translucent fill (no outline).
        const bool mute_focus = selected_column && state.rows[state.selected].kind == device_ui::Kind::mute;
        fill(card, gray(1, 0.045f)); outline(card, gray(1, 0.08f));
        if (slider_focus) {
            const auto slider = layout.slider(column);
            const float half = (slider.bottom - slider.top) * 0.5f; // A pill, as the frame on it.
            if (editing) fill(slider, uiAccent(0.07f), half);
            if (!frame) fill(slider, gray(1, editing ? 0.05f : 0.12f), half);
        }
        std::wstring name = column ? tr(L"Brak wejścia audio") : tr(L"Brak wyjścia audio");
        for (const auto& endpoint : state.data.endpoints) if (endpoint.input == (column != 0) && endpoint.active) { name = endpoint.name; break; }
        // Right: mute button with the level centred beneath it. Left: title and device as one
        // group, centred on that column.
        const float level_y = mute.bottom + 20;
        textLine(channel.available ? std::to_wstring(static_cast<int>(channel.volume * 100 + 0.5f)) + L"%" : L"—",
                 mute.left - 24, level_y, mute.right - mute.left + 48, action_font_.Get(), channel.available ? gray(0.9f) : gray(0.4f));
        const float group_y = (mute.top + level_y + typeMetrics(action_font_.Get()).cap * 0.5f) * 0.5f;
        textGroup(column ? tr(L"Nagrywanie") : tr(L"Odtwarzanie"), name, card.left + 26, group_y, mute.left - 40 - card.left - 26,
                  category_font_.Get(), gray(0.96f), settings_body_font_.Get(), gray(0.58f), 1);
        // A round glass button; muted, its glyph keeps the accent unless focused.
        const auto mute_ink = drawButton(box(mute), mute_focus ? arrival(context_->device_glide, mute) : 0.0f, 1, false, false, channel.available);
        drawAudioIcon((mute.left + mute.right) * 0.5f, (mute.top + mute.bottom) * 0.5f, column != 0, channel.muted,
                      channel.available && channel.muted && !mute_focus ? uiAccent() : mute_ink);
        const float y = (track.top + track.bottom) * 0.5f;
        const float thickness = editing ? 4.0f : 2.0f;
        fill({track.left, y - thickness, track.right, y + thickness}, gray(1, channel.available ? 0.15f : 0.06f), thickness);
        if (channel.available) {
            const float position = std::lerp(track.left, track.right, std::clamp(channel.volume, 0.0f, 1.0f));
            if (position > track.left) fill({track.left, y - thickness, position, y + thickness}, channel.muted ? gray(0.47f) : uiAccent(), thickness);
            brush(gray(0.96f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(position, y), editing ? 9.0f : 6.0f, editing ? 9.0f : 6.0f), solid_.Get());
        }
        text(column ? tr(L"Wejścia") : tr(L"Wyjścia"), card.left + 2, 376, layout.columnWidth() - 4, 28, settings_body_font_.Get(), gray(0.53f));
        target_->PushAxisAlignedClip(D2D1::RectF(card.left - 3, layout.list_top - 2, card.right + 3, layout.bottom() + 2), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        bool any = false;
        for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
            const auto& row = state.rows[i];
            if (row.kind != device_ui::Kind::endpoint || row.input != (column != 0)) continue;
            any = true;
            const auto r = layout.row(state, i);
            if (r.bottom < layout.list_top || r.top > layout.bottom()) continue;
            const bool focus = state.selected == i && !frame;
            fill(r, row.checked ? uiAccent(0.07f) : gray(1, 0.025f));
            if (focus) fill(r, gray(1, 0.1f)); // No outline.
            const auto center = D2D1::Point2F(r.left + 25, (r.top + r.bottom) * 0.5f);
            brush(row.checked ? uiAccent() : gray(1, 0.25f));
            target_->DrawEllipse(D2D1::Ellipse(center, 6, 6), solid_.Get(), 1.4f);
            if (row.checked) target_->FillEllipse(D2D1::Ellipse(center, 3, 3), solid_.Get());
            textGroup(row.title, row.checked ? std::wstring(tr(L"Domyślne")) : std::wstring{}, r.left + 48, center.y, r.right - r.left - 70,
                      menu_font_.Get(), gray(row.checked ? 0.97f : 0.8f), toast_sub_font_.Get(), uiAccent(0.85f), 1);
        }
        if (!any) text(tr(L"Brak dostępnych urządzeń"), card.left + 20, layout.list_top + 22, layout.columnWidth() - 40, 36, settings_body_font_.Get(), gray(0.42f));
        target_->PopAxisAlignedClip();
        if (const float maximum = layout.maximum(state, column); maximum > 0) {
            const float length = layout.bottom() - layout.list_top, thumb = std::max(24.0f, length * length / (length + maximum));
            const float top = layout.list_top + (length - thumb) * state.audio_scroll[column] / maximum;
            fill({card.right + 9, top, card.right + 12, top + thumb}, gray(1, 0.27f), 1.5f);
        }
    }
    for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) if (state.audioFooter(i)) {
        const auto r = layout.row(state, i);
        const auto ink = drawButton(box(r), state.selected == i ? arrival(context_->device_glide, r) : 0.0f, 1, false);
        const float inset = (r.bottom - r.top) * 0.5f - 6; // Clear of the rounded ends.
        text(state.rows[i].title, r.left + inset, r.top, r.right - r.left - 2 * inset, r.bottom - r.top, action_font_.Get(), ink);
    }
    text(state.data.status, layout.left(), viewport_.height - 49, layout.panelWidth(), 30, settings_body_font_.Get(), gray(0.62f));
}
