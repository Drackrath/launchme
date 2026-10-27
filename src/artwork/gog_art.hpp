#pragma once
#include "steamgriddb_search.hpp"

#include <filesystem>
#include <string>
#include <string_view>

// GOG games' official art from GOG's public product API (no account): api.gog.com/v2/games/<id> links
// the box art (tall, up to 342×482), the Galaxy background (wide, ~2560 px) and a transparent logo PNG.
// JPEG/PNG links only. Kept per product in <cache>\..\artwork\gog-art\<id>.v1 for 30 days; a product
// GOG does not know (404) for a week; a failed request not at all.
struct GogArt { std::string tall, wide, logo; };
GogArt parseGogArt(std::string_view v2_games);
GogArt gogArt(const std::string& product_id, const std::filesystem::path& cache, const ArtworkHttp& get = httpGet);
