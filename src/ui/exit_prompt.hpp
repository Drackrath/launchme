#pragma once

#include "../i18n/i18n.hpp"
#include "settings_view.hpp"

#include <array>

// Asked when the launcher closes while quick resume keeps games suspended.
// One short question and three stacked buttons; drawing and hit-testing share this layout.
namespace exit_prompt {
enum class Choice { resume, keep, cancel };
inline constexpr auto question = trKey(L"Wznowić gry przed wyjściem?");
inline constexpr std::array<const wchar_t*, 3> labels{trKey(L"Wznów gry"), trKey(L"Zostaw wstrzymane"), trKey(L"Anuluj")};
inline constexpr int count = static_cast<int>(labels.size());
struct Layout {
    float width, height;
    settings_ui::Rect panel() const {
        const float w = std::min(480.0f, width - 64), h = 264;
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    settings_ui::Rect button(float index) const {
        const auto p = panel();
        const float top = p.top + 88 + index * 54;
        return {p.left + 24, top, p.right - 24, top + settings_ui::Layout::button_height};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < count; ++i) if (button(static_cast<float>(i)).contains(x, y)) return i;
        return -1;
    }
};
} // namespace exit_prompt
