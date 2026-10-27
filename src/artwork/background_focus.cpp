#include "background_focus.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
std::filesystem::path focusFile(const Game& game, const std::filesystem::path& cache) {
    const auto key = gameCacheKey(game);
    if (key.empty()) return {};
    return cache.parent_path() / L"artwork" / L"focus" / (text_encoding::wide(key) + L".background");
}
}

uint64_t backgroundHash(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash ^ bytes.size();
}

std::optional<CropFocus> backgroundFocus(const Game& game, std::span<const uint8_t> bytes, const std::filesystem::path& cache) {
    const auto file = focusFile(game, cache);
    if (file.empty() || bytes.empty()) return std::nullopt;
    std::ifstream input(file);
    int version = 0;
    std::string hash;
    CropFocus focus;
    if (!(input >> version >> hash >> focus.x >> focus.y) || version != 1 || !std::isfinite(focus.x) || !std::isfinite(focus.y)) return std::nullopt;
    char expected[17]{};
    std::snprintf(expected, sizeof(expected), "%016llx", static_cast<unsigned long long>(backgroundHash(bytes)));
    if (hash != expected) return std::nullopt; // Saved for another picture of this game.
    focus.x = std::clamp(focus.x, 0.0f, 1.0f);
    focus.y = std::clamp(focus.y, 0.0f, 1.0f);
    return focus;
}

bool saveBackgroundFocus(const Game& game, std::span<const uint8_t> bytes, CropFocus focus, const std::filesystem::path& cache) {
    const auto file = focusFile(game, cache);
    if (file.empty() || bytes.empty() || !std::isfinite(focus.x) || !std::isfinite(focus.y)) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    char hash[17]{};
    std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(backgroundHash(bytes)));
    std::ostringstream text;
    text << "1 " << hash << ' ' << std::clamp(focus.x, 0.0f, 1.0f) << ' ' << std::clamp(focus.y, 0.0f, 1.0f) << '\n';
    return atomic_file::write(file, text.str());
}

bool clearBackgroundFocus(const Game& game, const std::filesystem::path& cache) {
    const auto file = focusFile(game, cache);
    if (file.empty()) return false;
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return !ec;
}
