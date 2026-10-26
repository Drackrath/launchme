// Co nowego after an update (src/app/updates.cpp): a panel over the dimmed screen with the new
// version, its changes as wrapped text that scrolls, and OK. Geometry: ui/update_view.hpp.
#include "renderer.hpp"

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
D2D1_RECT_F box(settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
}

void Renderer::drawWhatsNew() {
    if (!context_ || !context_->whats_new) return;
    auto& state = *context_->whats_new;
    const update_ui::WhatsNewLayout layout{viewport_.width, viewport_.height};
    brush(D2D1::ColorF(0, 0, 0, 0.6f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const auto p = layout.panel();
    brush(D2D1::ColorF(0.055f, 0.062f, 0.078f, 0.985f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(box(p), 24, 24), solid_.Get());
    brush(gray(1, 0.08f));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(box(p), 24, 24), solid_.Get(), 1);
    // The heading: an accent mark, the version and where it came from.
    brush(uiAccent());
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p.left + 44, p.top + 40, p.left + 50, p.top + 108), 3, 3), solid_.Get());
    textLine(std::wstring(tr(L"Co nowego w Launchme ")) + state.version, p.left + 66, p.top + 58, p.right - p.left - 110, settings_heading_font_.Get(), gray(0.97f));
    textLine(tr(L"Zaktualizowano. Zmiany w tej wersji:"), p.left + 66, p.top + 96, p.right - p.left - 110, settings_body_font_.Get(), gray(0.55f));
    // The changes: wrapped, clipped to their area, scrolled.
    const auto area = layout.text();
    const float width = area.right - area.left;
    ComPtr<IDWriteTextLayout> text;
    const auto& body = state.text.empty() ? std::wstring(tr(L"Brak opisu zmian.")) : state.text;
    settings_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (SUCCEEDED(dwrite_->CreateTextLayout(body.c_str(), static_cast<UINT32>(body.size()), settings_body_font_.Get(), width, 100000, text.GetAddressOf()))) {
        DWRITE_TEXT_METRICS metrics{};
        text->GetMetrics(&metrics);
        state.content = metrics.height;
        state.scroll = std::clamp(state.scroll, 0.0f, layout.maxScroll(state.content));
        target_->PushAxisAlignedClip(box(area), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        brush(gray(0.86f));
        target_->DrawTextLayout(D2D1::Point2F(area.left, area.top - state.scroll), text.Get(), solid_.Get());
        target_->PopAxisAlignedClip();
    }
    settings_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // A thin scrollbar when it does not fit.
    const float maximum = layout.maxScroll(state.content);
    if (maximum > 0) {
        const float length = area.bottom - area.top;
        const float thumb = std::max(30.0f, length * length / (length + maximum));
        const float y = area.top + (length - thumb) * std::min(1.0f, state.scroll / maximum);
        brush(gray(1, 0.3f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(area.right + 18, y, area.right + 21, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
    // OK: the only button, so it has the focus.
    const auto ok = layout.ok();
    textLine(L"OK", ok.left, (ok.top + ok.bottom) * 0.5f, ok.right - ok.left, action_font_.Get(), drawButton(box(ok), 1.0f, 1, false),
             DWRITE_TEXT_ALIGNMENT_CENTER);
}
