#pragma once
#include "../platform/power.hpp"
#include "../i18n/i18n.hpp"
#include "settings_view.hpp"

#include <algorithm>
#include <vector>

// Guide hold and "Zasilanie" in the quick bar. Three groups with an empty band between them: the
// launcher (entering or leaving Tryb TV first, then closing the launcher), the PC's own power
// actions offered on this machine (in red: sleep, hibernate, restart, shut down), Cancel last.
// The rows follow the Settings choice list's measures (Layout::picker / choice).
namespace power_menu {
// The first four follow PowerAction (static_cast between them).
enum class Item { sleep, hibernate, restart, shutdown, exit, tv_mode, cancel };

inline bool systemAction(Item item) { return item != Item::exit && item != Item::tv_mode && item != Item::cancel; }
// 0: the launcher, 1: the PC's power, 2: Cancel.
inline int group(Item item) { return systemAction(item) ? 1 : item == Item::cancel ? 2 : 0; }
// Drawn in red: the actions that put the PC to sleep, restart it or turn it off.
inline bool danger(Item item) { return systemAction(item); }

inline std::vector<Item> items(const std::vector<PowerAction>& available) {
    std::vector<Item> result{Item::tv_mode, Item::exit};
    for (const auto action : available) result.push_back(static_cast<Item>(action));
    result.push_back(Item::cancel);
    return result;
}
// `desktop`: the launcher shows the desktop window, so the mode row enters Tryb TV.
inline const wchar_t* label(Item item, bool desktop = false) {
    switch (item) {
    case Item::sleep: return tr(L"Uśpij");
    case Item::hibernate: return tr(L"Hibernuj");
    case Item::restart: return tr(L"Uruchom ponownie");
    case Item::shutdown: return tr(L"Wyłącz PC");
    case Item::exit: return tr(L"Zamknij launcher");
    case Item::tv_mode: return desktop ? tr(L"Przejdź do trybu TV") : tr(L"Wyjdź z trybu TV");
    case Item::cancel: return tr(L"Anuluj");
    }
    return L"";
}
// gaps[i]: row i starts a new group (an empty band above it).
inline std::vector<bool> gaps(const std::vector<Item>& items) {
    std::vector<bool> result(items.size(), false);
    for (size_t i = 1; i < items.size(); ++i) result[i] = group(items[i]) != group(items[i - 1]);
    return result;
}

// Tryb TV's menu: the picker's panel grown by the bands, its rows pushed down past them. Drawing,
// clicks and the pointer's hand all measure here.
struct Geometry {
    float width = 1600, height = 900;
    std::vector<bool> gaps;
    static constexpr float row_step = 54, row_height = 50, band = 18, header = 83;
    int count() const { return static_cast<int>(gaps.size()); }
    float bandsAbove(int row) const {
        float total = 0;
        for (int i = 1; i <= std::min(row, count() - 1); ++i) if (gaps[static_cast<size_t>(i)]) total += band;
        return total;
    }
    settings_ui::Rect panel() const {
        const float h = 100.0f + count() * row_step + bandsAbove(count() - 1);
        const float w = std::min(620.0f, width - 96);
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    settings_ui::Rect row(int index) const {
        const auto p = panel();
        const float top = p.top + header + index * row_step + bandsAbove(index);
        return {p.left + 18, top, p.right - 18, top + row_height};
    }
    // A fractional row (the highlight gliding between two rows).
    settings_ui::Rect rowAt(float position) const {
        if (count() <= 0) return {};
        position = std::clamp(position, 0.0f, static_cast<float>(count() - 1));
        const int below = static_cast<int>(position), above = std::min(below + 1, count() - 1);
        const float t = position - below;
        const auto a = row(below), b = row(above);
        return {a.left, a.top + (b.top - a.top) * t, a.right, a.bottom + (b.bottom - a.bottom) * t};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < count(); ++i) if (row(i).contains(x, y)) return i;
        return -1;
    }
};
} // namespace power_menu
