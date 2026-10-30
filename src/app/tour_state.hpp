#pragma once
// The first-start tour in Launcher (src/app/tour.cpp, src/ui/tour_view.hpp).
#include "../ui/settings_view.hpp"

#include <cstdint>

namespace launcher_state {
struct TourState {
    bool open = false;
    int step = 0;            // tour::Step.
    int button = 2;          // tour::Button with keyboard/pad focus.
    bool tiles = true;       // Last step: the layout tiles have the focus (false: the buttons).
    int icon = 0;            // Quick bar step: the icon being described.
    uint64_t step_at = 0, icon_at = 0;
    float alpha = 0;         // Overlay fade in and out.
    int hover_button = -1, hover_tile = -1;
    settings_ui::LayoutPreview preview;
};
} // namespace launcher_state
