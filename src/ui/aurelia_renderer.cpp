#include "renderer.hpp"
#include <algorithm>

namespace {
using palette::gray;
D2D1_RECT_F box(settings_ui::Rect r) { return {r.left, r.top, r.right, r.bottom}; }
}
void Renderer::drawAurelia() {
    if (!context_ || !context_->aurelia || !context_->aurelia->open || !createProfileFonts()) return;
    auto& s = *context_->aurelia; const aurelia_ui::Layout layout{viewport_.width, viewport_.height};
    const auto p = layout.panel(); const float width = p.right - p.left;
    const D2D1_COLOR_F gold = D2D1::ColorF(0.93f, 0.73f, 0.42f);
    brush(gray(0, 0.66f)); target_->FillRectangle({0, 0, viewport_.width, viewport_.height}, solid_.Get());
    brush(D2D1::ColorF(0.038f, 0.046f, 0.064f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(p), 24, 24), solid_.Get());
    brush(D2D1::ColorF(gold.r, gold.g, gold.b, 0.28f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(p), 24, 24), solid_.Get(), 1.5f);
    brush(gold); target_->FillRoundedRectangle(D2D1::RoundedRect({p.left + 28, p.top + 26, p.left + 33, p.top + 82}, 2.5f, 2.5f), solid_.Get());
    textLine(s.title, p.left + 50, p.top + 43, width - 150, profile_value_font_.Get(), gray(0.96f));
    textLine(s.subtitle, p.left + 50, p.top + 78, width - 150, profile_body_font_.Get(), gray(0.64f));
    const auto close = layout.close();
    textLine(L"×", close.left, (close.top + close.bottom) / 2, close.right - close.left, profile_value_font_.Get(),
             drawButton(box(close), s.hover == -1 ? 0.7f : 0, 1, false), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (s.page == aurelia_ui::Page::setup) {
        const D2D1_RECT_F notice{p.left + 28, p.top + 104, p.right - 28, p.top + 224};
        brush(D2D1::ColorF(gold.r, gold.g, gold.b, 0.065f)); target_->FillRoundedRectangle(D2D1::RoundedRect(notice, 12, 12), solid_.Get());
        textLine(L"SINGLE PLAYER / OFFLINE", notice.left + 18, notice.top + 23, width - 100, profile_title_font_.Get(), gold);
        paragraph(aurelia_ui::notice(), notice.left + 18, notice.top + 43, notice.right - notice.left - 36, profile_body_font_.Get(), gray(0.78f), 4);
    }
    if (s.confirmation) {
        const auto& question = *s.confirmation;
        text(question.title, p.left + 44, p.top + 260, width - 88, 100, profile_value_font_.Get(), gray(0.97f));
        text(question.description, p.left + 44, p.top + 370, width - 88, 130, profile_body_font_.Get(), gray(0.7f));
        const auto cancel = layout.input(), yes = layout.send();
        textLine(tr(L"Anuluj"), cancel.left, (cancel.top + cancel.bottom) / 2, cancel.right - cancel.left, profile_title_font_.Get(),
                 drawButton(box(cancel), s.hover != -5 ? 1.0f : 0, 1, false), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(tr(L"Potwierdź"), yes.left, (yes.top + yes.bottom) / 2, yes.right - yes.left, profile_title_font_.Get(),
                 drawButton(box(yes), s.hover == -5 ? 1.0f : 0, 1, true), DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    if (s.page == aurelia_ui::Page::chat) {
        const auto area = layout.log(); target_->PushAxisAlignedClip(box(area), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        struct Bubble { Microsoft::WRL::ComPtr<IDWriteTextLayout> text; float height = 0; };
        std::vector<Bubble> bubbles(s.messages.size());
        const float bubble_width = (area.right - area.left) * 0.83f;
        s.chat_content = 0;
        profile_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        for (size_t i = 0; i < s.messages.size(); ++i) {
            const auto& message = s.messages[i]; auto& b = bubbles[i];
            if (SUCCEEDED(dwrite_->CreateTextLayout(message.text.c_str(), static_cast<UINT32>(message.text.size()), profile_body_font_.Get(), bubble_width - 32, 100000, b.text.GetAddressOf()))) {
                DWRITE_TEXT_METRICS metrics{}; b.text->GetMetrics(&metrics); b.height = std::max(54.0f, metrics.height + 48);
            }
            s.chat_content += b.height + 10;
        }
        profile_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const float maximum = std::max(0.0f, s.chat_content - (area.bottom - area.top));
        s.chat_scroll = s.chat_follow ? maximum : std::clamp(s.chat_scroll, 0.0f, maximum);
        float y = area.top - s.chat_scroll;
        for (size_t i = 0; i < s.messages.size(); ++i) {
            const auto& message = s.messages[i]; const auto& b = bubbles[i]; const auto h = b.height;
            const float x = message.self ? area.right - bubble_width : area.left;
            if (y + h < area.top || y > area.bottom || !b.text) { y += h + 10; continue; }
            brush(message.self ? D2D1::ColorF(0.18f, 0.15f, 0.11f) : gray(0.11f));
            target_->FillRoundedRectangle(D2D1::RoundedRect({x, y, x + bubble_width, y + h}, 12, 12), solid_.Get());
            textLine(message.self ? tr(L"Ty") : s.friend_name, x + 16, y + 18, bubble_width - 32, profile_small_font_.Get(), message.self ? gold : gray(0.6f));
            brush(gray(0.92f)); target_->DrawTextLayout({x + 16, y + 33}, b.text.Get(), solid_.Get());
            y += h + 10;
        }
        if (s.messages.empty()) textLine(s.busy ? tr(L"Wczytywanie rozmowy…") : tr(L"Brak wiadomości"), area.left, area.top + 35, area.right - area.left, profile_body_font_.Get(), gray(0.55f));
        target_->PopAxisAlignedClip();
    } else {
        for (int i = s.first; i < std::min(static_cast<int>(s.rows.size()), s.first + layout.visible(s)); ++i) {
            const auto& row = s.rows[i]; const auto r = layout.row(s, i);
            const bool focused = !s.input_focus && s.focus == i, hover = s.hover == i;
            brush(focused ? D2D1::ColorF(0.17f, 0.15f, 0.12f) : gray(1, hover ? 0.08f : 0.035f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 12, 12), solid_.Get());
            if (focused) { brush(gold); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), 12, 12), solid_.Get(), 1.5f); }
            float left = r.left + 18;
            if (row.toggle) {
                const D2D1_RECT_F check{left, r.top + 18, left + 24, r.top + 42};
                brush(row.checked ? gold : gray(0.16f)); target_->FillRoundedRectangle(D2D1::RoundedRect(check, 6, 6), solid_.Get());
                brush(row.checked ? gray(0.08f) : gray(0.5f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(check, 6, 6), solid_.Get(), 1);
                if (row.checked) {
                    target_->DrawLine({left + 5, r.top + 30}, {left + 10, r.top + 35}, solid_.Get(), 2);
                    target_->DrawLine({left + 10, r.top + 35}, {left + 19, r.top + 25}, solid_.Get(), 2);
                }
                left += 40;
            }
            const float reserved = row.value.empty() ? 20.0f : 230.0f;
            const auto ink = row.locked ? gray(0.4f) : row.danger ? D2D1::ColorF(1, 0.45f, 0.42f) : gray(0.94f);
            textLine(row.title, left, r.top + (row.description.empty() ? 30 : 21), r.right - left - reserved, profile_title_font_.Get(), ink);
            if (!row.description.empty()) textLine(row.description, left, r.top + 43, r.right - left - reserved, profile_small_font_.Get(), gray(row.locked ? 0.35f : 0.58f));
            if (!row.value.empty()) textLine(row.value, r.right - 226, r.top + 30, 208, profile_body_font_.Get(), gold, DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        if (static_cast<int>(s.rows.size()) > layout.visible(s)) {
            const float track = layout.visible(s) * 68.0f, thumb = track * layout.visible(s) / static_cast<float>(s.rows.size());
            const float y = layout.rowsTop(s) + (track - thumb) * s.first / std::max(1, static_cast<int>(s.rows.size()) - layout.visible(s));
            brush(gray(1, 0.3f)); target_->FillRoundedRectangle(D2D1::RoundedRect({p.right - 17, y, p.right - 14, y + thumb}, 1.5f, 1.5f), solid_.Get());
        }
    }
    if (!s.input_hint.empty()) {
        const auto input = layout.input(), send = layout.send();
        brush(gray(0.08f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(input), 10, 10), solid_.Get());
        brush(s.input_focus ? gold : gray(0.3f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(input), 10, 10), solid_.Get(), 1.2f);
        std::wstring shown = s.input.empty() ? s.input_hint : s.input;
        if (shown.size() > 110) shown = L"…" + shown.substr(shown.size() - 110);
        textLine(shown, input.left + 14, (input.top + input.bottom) / 2, input.right - input.left - 28, profile_body_font_.Get(), gray(s.input.empty() ? 0.5f : 0.95f));
        textLine(s.sending ? tr(L"Wysyłanie…") : s.page == aurelia_ui::Page::chat ? tr(L"Wyślij") : tr(L"Wykonaj"), send.left, (send.top + send.bottom) / 2,
                 send.right - send.left, profile_title_font_.Get(), drawButton(box(send), s.hover == -3 ? 1.0f : 0, 1, false), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(s.status, p.left + 30, p.top + 113, width - 60, profile_small_font_.Get(), gold);
    } else textLine(s.status.empty() ? tr(L"Enter / A: wybierz · Esc / B: wstecz · PgUp / PgDn: przewijaj") : s.status,
                    p.left + 30, p.bottom - 51, width - 60, profile_body_font_.Get(), s.status.empty() ? gray(0.5f) : gold);
    if (s.progress >= 0) {
        brush(gold); target_->FillRoundedRectangle(D2D1::RoundedRect({p.left + 30, p.bottom - 21, p.left + 30 + (width - 60) * std::clamp(s.progress, 0.0f, 1.0f), p.bottom - 17}, 2, 2), solid_.Get());
    }
}
