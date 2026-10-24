#pragma once

#include "../model/game.hpp"
#include "../model/game_rating.hpp"
#include "../net/http.hpp"
#include <atomic>
#include <chrono>
#include <functional>

namespace ratings {
struct Result {
    bool valid = false; // A successful, understood response, including no match / no reviews.
    std::optional<Rating> rating;
};
Result parseSteam(std::string_view body);
Result parseRawg(const Game& game, std::string_view body);
// An explicit identification may use a different title; preserve that binding across renames.
bool saveIdentified(const std::filesystem::path& cache, const Game& game, const Rating& rating);
std::wstring normalizedTitle(std::wstring_view name);
int rawgPlatform(const Game& game); // 0 = no safe platform mapping; title must still match exactly.

struct Limits {
    size_t per_source = 20; // At most 20 Steam + 20 RAWG requests per startup pass.
    std::chrono::milliseconds spacing{750};
};
struct Backend {
    // Injectable for offline tests. Private GET disables redirects for RAWG's key-bearing URL.
    std::function<HttpResponse(const std::wstring&, bool private_request)> get;
    std::function<uint64_t()> now;
    std::function<bool(std::chrono::milliseconds)> wait;
};
using Publish = std::function<void(Map)>;
// Worker only. First publishes all cached scores, then fills missing entries in small batches.
// Positive results persist; missing matches/scores retry after 7 days. Errors are not "missing".
void collect(std::vector<Game> games, const std::filesystem::path& cache, const std::wstring& rawg_key,
             const std::atomic_bool& alive, const Publish& publish, const Backend& backend = {}, Limits limits = {});
}
