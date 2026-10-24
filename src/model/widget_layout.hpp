#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace widgets {
enum class Size { icon, square, wide };
struct Preference {
    std::string id;
    Size size = Size::square;
    bool operator==(const Preference&) const = default;
};
inline bool validId(std::string_view id) {
    return !id.empty() && id.size() <= 161 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
    });
}
inline const char* name(Size size) { return size == Size::icon ? "icon" : size == Size::wide ? "wide" : "square"; }
inline Size next(Size size) { return size == Size::icon ? Size::square : size == Size::square ? Size::wide : Size::icon; }
}
