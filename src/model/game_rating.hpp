#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace ratings {
enum class Source { steam, rawg };
struct Rating {
    Source source = Source::steam;
    double score = 0; // Steam: percent positive, RAWG: /5. Never combine the scales.
    uint32_t count = 0;
    std::optional<int> metacritic;
    uint32_t rawg_id = 0;
    std::string slug;
    bool available() const { return count > 0 || metacritic.has_value(); }
};
using Map = std::unordered_map<std::string, Rating>; // By gameKey, independent of account / UI language.
std::wstring label(const Rating& rating);
std::wstring compactLabel(const Rating& rating); // List column: preserve %, /5 and /100 as distinct scales.
std::wstring url(const Rating& rating, uint32_t steam_id);
}
