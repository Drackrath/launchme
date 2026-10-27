#pragma once
#include "text_crop.hpp"
#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

// A game's background is drawn cover-fit: a 3840×1240 hero on a 16:9 screen shows 57% of its width.
// "Popraw kadr" on Tło ("Zmień grafikę") picks which part: the focus (the visible part's centre, 0…1
// of the picture) is kept per game in <config>\artwork\focus\<key>.background, bound to the
// picture's bytes, so another picture of the game starts centred again.
std::optional<CropFocus> backgroundFocus(const Game& game, std::span<const uint8_t> bytes, const std::filesystem::path& cache);
bool saveBackgroundFocus(const Game& game, std::span<const uint8_t> bytes, CropFocus focus, const std::filesystem::path& cache);
bool clearBackgroundFocus(const Game& game, const std::filesystem::path& cache);
uint64_t backgroundHash(std::span<const uint8_t> bytes); // FNV-1a 64 of the picture's bytes.
