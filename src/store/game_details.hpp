#pragma once
#include "../model/game_details.hpp"
#include <cstdint>
#include <filesystem>
#include <string_view>

GameDetails parseGameDetails(uint32_t app_id, std::string_view bytes);
GameDetails fetchGameDetails(uint32_t app_id, const std::filesystem::path& cache);
