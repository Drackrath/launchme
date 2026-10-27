#include "renderer.hpp"

#include <algorithm>

void Renderer::drawCropEditor() {
    if (!context_->crop_editor) return;
    const auto& state = *context_->crop_editor;
    // In a desktop sheet its title and ✕ stand for the heading and Back.
    const bool desktop = context_->desktop_embed != nullptr;
    const crop_ui::Layout layout{viewport_.width, viewport_.height, desktop, state.background ? state.aspect : 1.0f};
    const auto white = palette::text(), muted = palette::muted();
    if (!desktop) {
        const auto back = layout.back();
        // Back: a glass circle with a dot, white while it has the focus (Up from "Automatyczny").
        const auto circle = backCircle(back.left, back.top, back.right, back.bottom);
        const auto back_ink = drawButton(circle, state.control == crop_ui::State::back ? 1.0f : 0.0f, 1, false);
        const float back_y = (back.top + back.bottom) * 0.5f;
        brush(back_ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((circle.left + circle.right) * 0.5f, back_y), 3.5f, 3.5f), solid_.Get());
        // The heading sits on the back button's centre line (as in the device panels).
        textLine(state.background ? tr(L"Kadr tła") : tr(L"Kadr"), back.right + 28, back_y, 800, settings_heading_font_.Get(), white);
    }
    const auto preview = layout.preview();
    ID2D1Bitmap* background = state.background && !heroes_.empty() ? heroes_[0].bitmap.Get() : nullptr;
    if (!state.loading && state.error.empty() && state.background && background) {
        // What a screen shows of the background: the window at the focus, cover-fit into the preview.
        const auto pixels = background->GetSize();
        const float pw = preview.right - preview.left, ph = preview.bottom - preview.top;
        const float scale = std::max(pw / pixels.width, ph / pixels.height);
        const float w = pixels.width * scale, h = pixels.height * scale;
        const float x = preview.left + std::clamp(pw * 0.5f - state.focus.x * w, pw - w, 0.0f);
        const float y = preview.top + std::clamp(ph * 0.5f - state.focus.y * h, ph - h, 0.0f);
        target_->PushAxisAlignedClip(D2D1::RectF(preview.left, preview.top, preview.right, preview.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->DrawBitmap(background, D2D1::RectF(x, y, x + w, y + h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target_->PopAxisAlignedClip();
        brush(palette::gray(1, 0.18f));
        target_->DrawRectangle(D2D1::RectF(preview.left, preview.top, preview.right, preview.bottom), solid_.Get(), 1);
        if (state.control == -1 && !context_->desktop_window) {
            brush(white); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(preview.left - 8, preview.top - 8, preview.right + 8, preview.bottom + 8), 12, 12), solid_.Get(), 2);
        }
    } else if (!state.background && cards_ && !cards_->empty() && !state.loading && state.error.empty()) {
        auto card = (*cards_)[0]; card.x = preview.left; card.y = preview.top; card.size = layout.size(); card.opacity = card.cover_opacity = 1;
        drawCard(card, 0);
        if (state.control == -1 && !context_->desktop_window) { // The pad's focus ring; the window's mouse needs none.
            brush(white); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(preview.left - 8, preview.top - 8, preview.right + 8, preview.bottom + 8), 16, 16), solid_.Get(), 2);
        }
    } else text(state.loading || state.error.empty() ? std::wstring(tr(L"Wczytywanie…")) : state.error, preview.left, preview.top + layout.size() * 0.4f,
                preview.right - preview.left, 110, menu_font_.Get(), muted);
    const auto title = layout.title();
    settings_heading_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    text(state.game.name, title.left, title.top, title.right - title.left, title.bottom - title.top, settings_heading_font_.Get(), white);
    settings_heading_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // Glass pills, the focused one white; dimmed while the image is loading or failed.
    const bool enabled = !state.loading && state.error.empty();
    for (int i = 0; i < 2; ++i) {
        const auto r = layout.button(i);
        const auto ink = drawButton(D2D1::RectF(r.left, r.top, r.right, r.bottom), state.control == i ? 1.0f : 0.0f, 1, false, false, enabled);
        const float inset = (r.bottom - r.top) * 0.5f; // Content keeps clear of the rounded ends.
        text(i == 0 ? (state.background ? tr(L"Wyśrodkuj") : tr(L"Automatyczny")) : tr(L"Zapisz kadr"), r.left + inset, r.top, r.right - r.left - 2 * inset,
             r.bottom - r.top, action_font_.Get(), ink);
    }
    if (!state.loading && state.error.empty() && !state.adjustable())
        text(state.background ? tr(L"Tło ma już kształt ekranu.") : tr(L"Okładka jest kwadratowa."), title.left, layout.button(1).bottom + 24,
             title.right - title.left, 30, settings_body_font_.Get(), muted);
}
