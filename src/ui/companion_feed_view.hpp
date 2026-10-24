#pragma once
#include "settings_view.hpp"
#include "spring.hpp"
#include "../net/companion_feed.hpp"
#include <map>
#include <memory>

namespace companion_ui {
struct Feed {
    companion::feed::State archive;
    std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> images;
    std::wstring status, input;
    std::string thread, heart;
    bool loaded = false, busy = false, configured = false, can_discuss = false, social_busy = false;
    bool exiting = false, dm_exit = false, animate = true;
    float scroll = 0, app_age = 0, heart_age = 1;
    Spring motion, entrance;
    int hover = -1, focus = 0; // 0 heart, 1 comments, 2 DM; thread: 3 input, 4 submit.
    bool ready() const { return !exiting && app_age >= 2 && std::abs(entrance.value - 1) < 0.025f; }
    bool typing() const { return ready() && !thread.empty() && focus == 3; }
};
struct FeedLayout {
    float width, height;
    static constexpr float phone_w = 440, phone_h = 880, top = 164, bottom = 818, gap = 14;
    static constexpr int no_hit = -1, close_hit = -2, input_hit = -3, send_hit = -4, thread_back = -5;
    float scale() const { return std::max(0.1f, std::min({1.15f, (height - 90) / phone_h, (width - 72) / phone_w})); }
    float left() const { return (width - phone_w * scale()) * 0.5f; }
    float upper() const { return (height - 40 - phone_h * scale()) * 0.5f; }
    static settings_ui::Rect close() { return {372, 66, 420, 114}; }
    static settings_ui::Rect content(std::string_view thread = {}) { return {16, top, 424, thread.empty() ? bottom : 716.0f}; }
    static settings_ui::Rect input() { return {28, 748, 352, 808}; }
    static settings_ui::Rect send() { return {360, 748, 414, 808}; }
    static settings_ui::Rect threadBack() { return {24, 718, 414, 746}; }
    static float textHeight(std::wstring_view text, size_t width = 38) {
        size_t lines = 0, from = 0;
        do {
            const auto end = text.find(L'\n', from);
            const auto length = (end == text.npos ? text.size() : end) - from;
            lines += std::max<size_t>(1, (length + width - 1) / width);
            if (end == text.npos) break;
            from = end + 1;
        } while (from <= text.size());
        return std::max(24.0f, static_cast<float>(lines) * 22);
    }
    static float captionHeight(const companion::feed::Post& post) { return std::max(60.0f, textHeight(post.text)); }
    static float photoHeight(const companion::feed::Post& post) {
        return post.width && post.height ? 392.0f * static_cast<float>(post.height) / post.width : 394.0f;
    }
    static float actionsTop(const companion::feed::Post& post) { return 60 + photoHeight(post) + 18 + captionHeight(post) + 10; }
    static float commentHeight(const companion::feed::Comment& c) { return 30 + textHeight(c.text, 36) + 14; }
    static float cardHeight(const companion::feed::Post& post, std::string_view thread = {}) {
        float h = actionsTop(post) + 56;
        if (post.id == thread) {
            h += 36;
            for (const auto& c : post.social.comments) h += commentHeight(c);
        }
        return h;
    }
    static float position(const companion::feed::State& archive, size_t index, std::string_view thread = {}) {
        float y = 0;
        for (size_t i = 0; i < std::min(index, archive.posts.size()); ++i) y += cardHeight(archive.posts[i], thread) + gap;
        return y;
    }
    static settings_ui::Rect card(const companion::feed::State& archive, size_t index, float scroll, std::string_view thread = {}) {
        const float y = top + position(archive, index, thread) - scroll;
        return {24, y, 416, y + cardHeight(archive.posts[index], thread)};
    }
    static settings_ui::Rect photo(const companion::feed::State& archive, size_t index, float scroll, std::string_view thread = {}) {
        const auto c = card(archive, index, scroll, thread); return {c.left, c.top + 60, c.right, c.top + 60 + photoHeight(archive.posts[index])};
    }
    static settings_ui::Rect action(const companion::feed::State& archive, size_t index, int button, float scroll, std::string_view thread = {}) {
        const auto c = card(archive, index, scroll, thread);
        const float y = c.top + actionsTop(archive.posts[index]), x = c.left + 12 + button * 124.0f;
        return {x, y, x + 120, y + 44};
    }
    static float limit(const companion::feed::State& archive, std::string_view thread = {}) {
        return std::max(0.0f, position(archive, archive.posts.size(), thread) - gap - (content(thread).bottom - top));
    }
    static std::pair<int, int> visible(const companion::feed::State& archive, float scroll, std::string_view thread = {}) {
        int first = 0, last = 0;
        float y = 0;
        for (const auto& post : archive.posts) {
            if (y > scroll + content(thread).bottom - top) break;
            const float next = y + cardHeight(post, thread) + gap;
            if (next <= scroll) ++first;
            ++last; y = next;
        }
        return {first, last};
    }
    int hit(float x, float y, const Feed& feed) const {
        if (feed.exiting || std::abs(feed.entrance.value - 1) >= 0.025f) return no_hit;
        x = (x - left()) / scale(); y = (y - upper()) / scale();
        if (close().contains(x, y)) return close_hit;
        if (!feed.ready()) return no_hit;
        if (!feed.thread.empty()) {
            if (input().contains(x, y)) return input_hit;
            if (send().contains(x, y)) return send_hit;
            if (threadBack().contains(x, y)) return thread_back;
        }
        if (!content(feed.thread).contains(x, y)) return no_hit;
        const auto [first, last] = visible(feed.archive, feed.motion.value, feed.thread);
        for (int i = first; i < last; ++i) for (int a = 0; a < 3; ++a)
            if (action(feed.archive, i, a, feed.motion.value, feed.thread).contains(x, y)) return i * 3 + a;
        return no_hit;
    }
};
}
