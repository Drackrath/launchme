#pragma once
#include "settings_view.hpp"
#include "spring.hpp"
#include "../net/glance.hpp"

namespace glance_ui {
using settings_ui::Rect;
inline const wchar_t* title(int section) {
    switch (section) {
    case 0: return tr(L"Nowości Steam");
    case 1: return tr(L"Nadchodzące premiery");
    case 2: return tr(L"Wiadomości");
    case 3: return tr(L"Ciekawostki");
    default: return tr(L"Twoje gry");
    }
}
inline int tabIndex(int section) { return section == static_cast<int>(glance::Section::owned) ? 0 : section + 1; }
inline int sectionAt(int tab) { return tab == 0 ? static_cast<int>(glance::Section::owned) : tab - 1; }
inline float ease(float t) { t = std::clamp(t,0.0f,1.0f); return 1-std::pow(1-t,3.0f); }
struct State {
    int section = static_cast<int>(glance::Section::owned), focus = -1; // -1 tabs, -2 back, -3 refresh; >=0 article.
    float scroll = 0;
    bool loading = false;
    glance::Feed feed;
    std::array<float,glance::calendar_limit> card_heights{};
    float measured_width = 0;
    int hover = -100, direction = 1;
    float content_age = 1;
    Spring scroll_motion, tab_motion;
    Spring back_focus, refresh_focus, tab_focus;
    std::array<Spring,glance::calendar_limit> emphasis;
    SpringBox focus_box;
    bool motion_valid = false, focus_valid = false;
    bool timeline() const { return section==static_cast<int>(glance::Section::upcoming); }
    float drawScroll() const { return motion_valid ? scroll_motion.value : scroll; }
    float appearance(size_t index) const { return ease((content_age - 0.025f*static_cast<float>(index % 3)) / 0.32f); }
    void revealContent() { content_age = 0; focus_valid = false; }
    bool step(float dt, float width, float height, bool animated);
};
// Glance's shortcut on the default home: atop the right column, or right under the companion's
// tile while she is on (companion_ui::glance_top).
inline constexpr float shortcut_top = 42, shortcut_height = 150;
inline Rect shortcut(float width, float hover = 0, float top = shortcut_top) {
    return {width - 356 - hover*3, top - hover*4, width - 56 + hover*3, top + shortcut_height};
}
struct Layout {
    float width, height;
    static constexpr float left = 56, gap = 18, top = 204, card_height = 282;
    int columns(const State* state = nullptr) const { return state && state->timeline() ? 1 : width >= 1250 ? 3 : 2; }
    float cardWidth() const { return (width - 2 * left - gap * (columns() - 1)) / columns(); }
    float bottom() const { return height - 85; }
    Rect back() const { return {left, 38, left + 54, 92}; }
    Rect refresh() const { return {width - left - 168, 41, width - left, 89}; }
    Rect tabPosition(float i) const {
        const float w = (width - 2 * left - (glance::section_count-1)*10) / glance::section_count;
        return {left + i * (w + 10), 116, left + i * (w + 10) + w, 162};
    }
    Rect tab(int section) const { return tabPosition(static_cast<float>(tabIndex(section))); }
    float cardHeight(int i, const State* state) const {
        return state && i >= 0 && i < static_cast<int>(state->card_heights.size()) && state->card_heights[i] > 0 ? state->card_heights[i] : card_height;
    }
    Rect card(int i, float scroll, const State* state = nullptr) const {
        if (state && state->timeline()) {
            float y=top-scroll;
            for (int row=0;row<=i;++row) {
                if (groupStart(row,*state)) y+=34;
                if (row<i) y+=128;
            }
            return {left+216,y,width-left,y+116};
        }
        const float x = left + (i % columns()) * (cardWidth() + gap);
        float y = top-scroll;
        for (int row = 0; row < i/columns(); ++row) {
            float tallest = 0;
            for (int col = 0; col < columns(); ++col) tallest = std::max(tallest,cardHeight(row*columns()+col,state));
            y += tallest+gap;
        }
        return {x, y, x + cardWidth(), y + cardHeight(i,state)};
    }
    bool groupStart(int i,const State& state) const {
        return i==0 || (i>0 && i<static_cast<int>(state.feed.items.size()) && state.feed.items[i].release.group()!=state.feed.items[i-1].release.group());
    }
    Rect visualCard(int i, const State& state) const {
        auto r = card(i,state.drawScroll(),&state);
        const float entering = 1-state.appearance(static_cast<size_t>(i));
        const float dx = entering*state.direction*18, dy = entering*12;
        r.left += dx; r.right += dx; r.top += dy; r.bottom += dy;
        return r;
    }
    float reveal(int i, int count, float scroll, const State* state = nullptr) const {
        if (i < 0) return 0;
        const auto r = card(i, scroll, state);
        if (r.top < top) scroll -= top - r.top;
        if (r.bottom > bottom()) scroll += r.bottom - bottom();
        float end = 0;
        for (int j = std::max(0,(count-1)/columns(state)*columns(state)); j < count; ++j) end = std::max(end,card(j,0,state).bottom);
        const float max = std::max(0.0f,end-bottom());
        return std::clamp(scroll, 0.0f, max);
    }
    int hit(float x, float y, const State& state) const {
        if (back().contains(x,y)) return -2;
        if (refresh().contains(x,y)) return -3;
        for (int i = 0; i < glance::section_count; ++i) if (tab(i).contains(x,y)) return -10-i;
        if (y >= top && y <= bottom())
            for (int i = 0; i < static_cast<int>(state.feed.items.size()); ++i)
                if (state.appearance(static_cast<size_t>(i)) > 0.1f && visualCard(i,state).contains(x,y)) return i;
        return -100;
    }
};

inline bool State::step(float dt, float width, float height, bool animated) {
    const Layout layout{width,height};
    bool moving = loading;
    if (animated && content_age < 1) { content_age = std::min(1.0f,content_age+dt); moving = true; }
    if (!animated) content_age = 1;
    const auto advance = [&](Spring& value,float target) {
        if (!motion_valid || !animated) value.snap(target);
        else value.step(target,dt,spring::glide.omega,spring::glide.zeta);
        if (!value.settled(target,0.002f)) moving = true; else value.snap(target);
    };
    advance(scroll_motion,scroll); advance(tab_motion,static_cast<float>(tabIndex(section)));
    advance(back_focus,focus == -2 ? 1.0f : 0.0f);
    advance(refresh_focus,focus == -3 ? 1.0f : 0.0f);
    advance(tab_focus,focus == -1 ? 1.0f : 0.0f);
    for (size_t i = 0; i < emphasis.size(); ++i)
        advance(emphasis[i],focus == static_cast<int>(i) ? 1.0f : hover == static_cast<int>(i) ? 0.45f : 0.0f);
    if (focus >= 0 && focus < static_cast<int>(feed.items.size())) {
        // Follow the row in content coordinates. Scrolling is applied exactly once when drawing.
        const auto r = layout.card(focus,0,this);
        if (!focus_valid || !animated) focus_box.snap(r.left,r.top,r.right,r.bottom);
        else focus_box.step(r.left,r.top,r.right,r.bottom,dt,spring::glide);
        if (!focus_box.settled(r.left,r.top,r.right,r.bottom)) moving = true;
        focus_valid = true;
    } else focus_valid = false;
    motion_valid = true;
    return moving;
}
}
