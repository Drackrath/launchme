#pragma once
#include "settings_view.hpp"
#include "../artwork/bulk_covers.hpp"

namespace bulk_covers_ui {
struct State {
    bool open = false, started = false, paused = false, cancelling = false;
    int focus = 0;
    bulk_covers::Progress progress;
    int buttons() const { return progress.terminal() ? 1 : 2; }
};
inline std::wstring duration(uint64_t seconds) {
    if (seconds >= 3600) return std::to_wstring(seconds / 3600) + L" h " + std::to_wstring(seconds / 60 % 60) + L" min";
    if (seconds >= 60) return std::to_wstring(seconds / 60) + L" min " + std::to_wstring(seconds % 60) + L" s";
    return std::to_wstring(seconds) + L" s";
}
inline const wchar_t* status(const State& s) {
    if (s.cancelling && !s.progress.terminal()) return tr(L"Anulowanie…");
    if (s.paused && s.started && !s.progress.terminal()) return tr(L"Wstrzymano pobieranie");
    switch (s.progress.phase) {
    case bulk_covers::Phase::scanning: return tr(L"Sprawdzanie…");
    case bulk_covers::Phase::ready: return tr(L"Gotowe do pobrania");
    case bulk_covers::Phase::waiting: return tr(L"Oczekiwanie na limit usługi");
    case bulk_covers::Phase::finished: return tr(L"Zakończono");
    case bulk_covers::Phase::cancelled: return tr(L"Anulowano");
    default: return tr(L"Pobieranie brakujących okładek…");
    }
}
inline const wchar_t* primary(const State& s) {
    return s.progress.terminal() ? tr(L"Zamknij") : !s.started ? tr(L"Pobierz") : s.paused ? tr(L"Wznów") : tr(L"Wstrzymaj");
}
inline std::wstring summary(const State& s) {
    const auto& p = s.progress;
    if (!p.terminal() && p.phase != bulk_covers::Phase::ready) return p.reason;
    std::wstring text;
    const auto add = [&](const wchar_t* label, size_t count) {
        if (!text.empty()) text += L" · ";
        text += std::wstring(label) + L": " + std::to_wstring(count);
    };
    if (p.terminal()) add(tr(L"Pobrane"), p.downloaded);
    else add(tr(L"Do pobrania"), p.total - p.existing);
    if (p.existing) add(tr(L"Już dostępne"), p.existing);
    if (p.missing) add(tr(L"Nieznalezione"), p.missing);
    if (p.failed) add(tr(L"Błędy"), p.failed);
    return text;
}
struct Layout {
    float width, height;
    int buttons = 2;
    settings_ui::Rect panel() const { const float w = std::min(780.0f, width - 48), h = std::min(340.0f, height - 48); return {(width-w)/2, (height-h)/2, (width+w)/2, (height+h)/2}; }
    settings_ui::Rect button(int i) const {
        if (i < 0 || i >= buttons) return {};
        const auto p = panel(); const float w = (p.right - p.left - 80) / 2;
        const float x = buttons == 1 ? (p.left + p.right - w) / 2 : p.left + 32 + i * (w + 16);
        return {x, p.bottom-78, x+w, p.bottom-26};
    }
    int hit(float x, float y) const { for (int i=0;i<buttons;++i) if (button(i).contains(x,y)) return i; return -1; }
};
}
