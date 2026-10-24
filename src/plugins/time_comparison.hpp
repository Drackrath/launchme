#pragma once
#include "json.hpp"
#include "../platform/text_encoding.hpp"
#include "../i18n/i18n.hpp"
#include <algorithm>
#include <optional>

namespace plugins {
// A native, read-only details/widget block. The plugin supplies reference times;
// the host supplies the current account's playtime and renders the comparison.
struct TimeComparison {
    std::string game_key;
    uint32_t main_seconds = 0, extra_seconds = 0, complete_seconds = 0;
    std::wstring status;
};
inline std::optional<TimeComparison> parseTimeComparison(const json::Value& response, const std::string& expected_key) {
    const auto* value = response.get("comparison");
    if (!value || value->type != json::Value::Type::object || wire::text(*value, "gameKey") != expected_key) return {};
    TimeComparison result; result.game_key = expected_key;
    for (const auto& [key, target] : {std::pair{"mainSeconds", &result.main_seconds}, {"extraSeconds", &result.extra_seconds}, {"completionSeconds", &result.complete_seconds}}) {
        const auto* v = value->get(key);
        if (!v) continue;
        if (v->type != json::Value::Type::number || !std::isfinite(v->numeric) || v->numeric < 0 || v->numeric > UINT32_MAX || std::floor(v->numeric) != v->numeric) return {};
        *target = static_cast<uint32_t>(v->numeric);
    }
    const auto status = wire::text(*value, "status");
    if (status.size() > 1024 || status.find('\0') != std::string::npos) return {};
    result.status = text_encoding::wide(status);
    if (!status.empty() && result.status.empty()) return {};
    return result;
}
inline std::wstring playtimeLabel(std::optional<uint64_t> seconds) {
    if (!seconds) return L"—";
    const auto minutes = *seconds / 60;
    if (minutes < 60) return std::to_wstring(minutes) + tr(L" min");
    return std::to_wstring(minutes / 60) + tr(L" godz.") + (minutes % 60 ? L" " + std::to_wstring(minutes % 60) + tr(L" min") : L"");
}
inline std::optional<double> timePercent(const TimeComparison& value, std::optional<uint64_t> played) {
    if (!value.main_seconds || !played) return {};
    return static_cast<double>(*played) * 100.0 / value.main_seconds;
}
inline std::wstring compactTime(std::optional<uint64_t> seconds) {
    if (!seconds) return L"—";
    const auto minutes = *seconds / 60;
    if (minutes < 60) return std::to_wstring(minutes) + L"m";
    return std::to_wstring(minutes / 60) + L"h" + (minutes % 60 ? std::to_wstring(minutes % 60) : L"");
}
inline std::wstring percentLabel(const TimeComparison& value, std::optional<uint64_t> played) {
    const auto percent = timePercent(value, played);
    return percent ? std::to_wstring(static_cast<uint64_t>(std::min(1e12, std::round(*percent)))) + L"%" : L"—";
}
inline std::wstring comparisonLabel(const TimeComparison& value, std::optional<uint64_t> played) {
    return playtimeLabel(played) + L" / " + playtimeLabel(value.main_seconds ? std::optional<uint64_t>(value.main_seconds) : std::nullopt) + L" · " + percentLabel(value, played);
}
}
