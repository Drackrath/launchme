#pragma once
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

namespace headphones {
struct State {
    bool connected = false;
    std::optional<int> battery;
    bool operator==(const State&) const = default;
};
inline constexpr auto name = L"Audio-Technica ATH-M50xBT2";
inline constexpr auto badge = L"headphones:ath-m50xbt2";
inline constexpr auto picture_url = L"https://www.audio-technica.com/media/converted/resized/3fee13f487241ee98f157c7fc15ff09f/1000x1000/catalog/product/a/t/ath-m50xbt_011x.webp";

// Windows exposes stereo, hands-free and LE_ aliases of the same headset.
// Match this model only, also inside a localized audio endpoint label.
inline bool supported(std::wstring_view endpoint_name) {
    std::wstring lower(endpoint_name);
    for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    constexpr std::wstring_view model = L"ath-m50xbt2";
    const auto at = lower.find(model);
    return at != lower.npos && (!at || !std::iswalnum(lower[at - 1])) &&
        (at + model.size() == lower.size() || !std::iswalnum(lower[at + model.size()]));
}
}
