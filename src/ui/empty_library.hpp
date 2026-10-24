#pragma once
// The TV home with no games at all (Domyślny, Flix, PSfive and snitch fall back to it; XMB and Metro
// always have their own tiles): why the library is empty — no Steam on this PC, Steam without a
// signed-in account, or simply nothing installed — and three ways on, as buttons the pad, the
// keyboard and the mouse all reach. Shared by drawing (home_chrome_renderer.cpp) and input
// (src/app/navigation.cpp, pointer.cpp).
#include "settings_view.hpp"

#include <algorithm>

namespace empty_library {
using settings_ui::Rect;
enum class Reason { loading, no_steam, no_account, empty };
// The buttons, left to right.
enum Action { add_game, platforms, refresh };
constexpr int action_count = 3;

struct Layout {
    float width = 1600, height = 900;
    float inset = 0; // The always-visible category rail on the left.
    static constexpr float panel_width = 780, panel_height = 372, button_width = 214, button_height = 52, button_gap = 16;
    float middle() const { return (width + inset) * 0.5f; }
    Rect panel() const {
        const float w = std::min(panel_width, width - inset - 96);
        const float top = std::max(120.0f, height * 0.5f - panel_height * 0.5f - 24);
        return {middle() - w * 0.5f, top, middle() + w * 0.5f, top + panel_height};
    }
    Rect button(int index) const {
        const auto p = panel();
        const float w = std::min(button_width, (p.right - p.left - 64 - (action_count - 1) * button_gap) / action_count);
        const float total = action_count * w + (action_count - 1) * button_gap;
        const float left = middle() - total * 0.5f + static_cast<float>(index) * (w + button_gap);
        return {left, p.bottom - 34 - button_height, left + w, p.bottom - 34};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < action_count; ++i) if (button(i).contains(x, y)) return i;
        return -1;
    }
};

inline const wchar_t* label(int action) {
    switch (action) {
    case add_game: return tr(L"Dodaj grę");
    case platforms: return tr(L"Platformy");
    default: return tr(L"Odśwież");
    }
}
inline const wchar_t* title(Reason reason) {
    switch (reason) {
    case Reason::no_steam: return tr(L"Brak gier w bibliotece");
    case Reason::no_account: return tr(L"Zaloguj się w Steam");
    default: return tr(L"Nie znaleziono gier");
    }
}
inline const wchar_t* body(Reason reason) {
    switch (reason) {
    case Reason::no_steam:
        return tr(L"Steam nie jest zainstalowany, a w innych sklepach nie ma gier. Dodaj grę z dysku albo włącz GOG, Epic, EA, Ubisoft, Battle.net lub Xbox w Platformach.");
    case Reason::no_account:
        return tr(L"Steam jest zainstalowany, ale nie ma w nim zalogowanego konta. Zaloguj się w kliencie Steam i odśwież albo dodaj gry z innych sklepów.");
    default:
        return tr(L"Zainstaluj grę w sklepie, włącz kolejne Platformy albo dodaj grę z dysku: program, skrót lub skrypt.");
    }
}
}
