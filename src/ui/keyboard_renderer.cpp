// On-screen keyboard: a small panel sliding up at the bottom, its field's text above the keys.
// Geometry: src/ui/onscreen_keyboard.hpp (shared with clicks and tests).
#include "renderer.hpp"

void Renderer::drawKeyboard() {
    const auto* state = context_->keyboard;
    const float progress = context_->keyboard_progress;
    if (!state || progress <= 0.001f) return;
    const osk::Layout layout{viewport_.width, viewport_.height};
    const auto shape = [](settings_ui::Rect r, float radius) { return D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius); };
    const float fade = std::clamp(progress, 0.0f, 1.0f);
    const auto panel = layout.panel(progress);
    brush(D2D1::ColorF(0.05f, 0.055f, 0.06f, 0.96f * fade));
    target_->FillRoundedRectangle(shape(panel, 18), solid_.Get());
    brush(D2D1::ColorF(1, 1, 1, 0.14f * fade));
    target_->DrawRoundedRectangle(shape(panel, 18), solid_.Get(), 1.0f);
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    // The field's text with a caret, its end kept in view; the field a pill.
    const auto preview = layout.previewRect(progress);
    brush(palette::gray(1, 0.06f * fade));
    target_->FillRoundedRectangle(pill(rect(preview)), solid_.Get());
    // A long text shows the part around the insertion point.
    const auto& value = context_->keyboard_text;
    const size_t caret = std::min(context_->keyboard_caret, value.size()), window = 60;
    const size_t start = value.size() <= window ? 0 : std::min(caret > 40 ? caret - 40 : 0, value.size() - window);
    const std::wstring shown = (start ? L"…" : L"") + value.substr(start, window);
    const size_t shown_caret = caret - start + (start ? 1 : 0);
    const float line = typeMetrics(menu_font_.Get()).line;
    fieldText(shown, shown_caret, std::fmod(elapsed_, 1.0f) < 0.6f, preview.left + 14, (preview.top + preview.bottom - line) * 0.5f,
              preview.right - preview.left - 28, line + 4, menu_font_.Get(), D2D1::ColorF(0.95f, 0.96f, 0.97f, fade));
    for (int row = 0; row < osk::rows; ++row) {
        const auto keys = osk::row(*state, row);
        for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
            const auto& key = keys[static_cast<size_t>(i)];
            const auto r = layout.key(*state, row, i, progress);
            const bool focused = row == state->row && i == state->key;
            const bool active = key.kind == osk::Kind::shift && state->shift;
            const bool special = key.kind != osk::Kind::character && key.kind != osk::Kind::space;
            // Keys are pills without an outline; the focused one solid white with dark ink.
            brush(focused ? palette::gray(0.96f, fade) : palette::gray(1, (active ? 0.28f : special ? 0.11f : 0.07f) * fade));
            target_->FillRoundedRectangle(pill(rect(r)), solid_.Get());
            std::wstring label;
            switch (key.kind) {
            case osk::Kind::character: label = std::wstring(1, osk::typed(*state, key.character)); break;
            case osk::Kind::shift: label = L"⇧"; break;
            case osk::Kind::layer: label = state->symbols ? L"abc" : L"?123"; break;
            case osk::Kind::space: label = tr(L"Spacja"); break;
            case osk::Kind::backspace: label = L"⌫"; break;
            case osk::Kind::done: label = tr(L"Gotowe"); break;
            }
            const auto ink = buttonInk(focused ? 1.0f : 0.0f, true, fade);
            textLine(label, r.left, (r.top + r.bottom) * 0.5f, r.right - r.left, action_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
}
