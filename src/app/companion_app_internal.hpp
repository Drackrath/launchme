#pragma once
// Helpers shared by the companion's Launcher files (companion.cpp, companion_request.cpp,
// companion_answer.cpp, companion_chat.cpp, companion_input.cpp, companion_mind_view.cpp): her
// identity, the pace of her typing and small text helpers. Not part of the Launcher interface.
#include "launcher.hpp"
#include "../net/companion_characters.hpp"
#include "../platform/ascii_text.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace companion_app {
// The chosen companion; none, an unknown or a "soon" one: the first of those that can be chosen
// by her key's alphabetical order (not by order=, which only arranges the Settings cards).
inline companion::Look lookOf(const AppSettings& settings) {
    if (const auto look = companion::lookFromKey(settings.companion_look); look && !companion::character(*look)->soon) return *look;
    const auto available = companion::availableLooks();
    if (available.empty()) return companion::Look{};
    return *std::min_element(available.begin(), available.end(), [](companion::Look a, companion::Look b) { return companion::lookKey(a) < companion::lookKey(b); });
}
inline bool english() { return i18n::english(); }
inline int dateKey(const SYSTEMTIME& time) { return time.wYear * 10000 + time.wMonth * 100 + time.wDay; }
// Her reply's messages in the log: a few alone, more with a guest.
inline constexpr size_t solo_messages = 3, duo_messages = 6;
// The typing pause before each waiting message of a reply, longer for a longer one.
inline float typingPause(const companion_ui::Message& message) { return std::clamp(0.45f + 0.012f * static_cast<float>(message.text.size()), 0.6f, 1.3f); }
// "Pisze…" (Settings: whole messages): how long the typing dots last before a message appears whole —
// longer for a longer one, as a person types; never a random wait (no slot-machine suspense) and never long.
inline float wholeTypingSeconds(const std::wstring& text) { return std::clamp(0.5f + static_cast<float>(text.size()) / 16.0f, 0.9f, 4.5f); }
// Characters per second of her speech; sentence ends and commas pause.
inline constexpr float speech_rate = 40;
inline float pause(wchar_t ch) { return ch == L'.' || ch == L'!' || ch == L'?' || ch == L'…' ? 7.0f : ch == L',' || ch == L';' || ch == L':' ? 3.0f : 1.0f; }
// How long her typewriter takes for a text (the pauses after punctuation included).
inline float typingSeconds(const std::wstring& text) {
    float cost = 0;
    for (size_t i = 0; i < text.size(); ++i) cost += i ? pause(text[i - 1]) : 1.0f;
    return cost / speech_rate;
}
inline std::wstring trimmed(std::wstring_view text) { return std::wstring(ascii_text::trim(text)); }
} // namespace companion_app
