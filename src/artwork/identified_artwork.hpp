#pragma once
#include "steamgriddb.hpp"
#include "text_crop.hpp"

struct IdentifiedArtwork {
    ArtworkKind kind = ArtworkKind::cover;
    std::vector<uint8_t> bytes;
    CropFocus focus;
};
struct IdentifiedArtworkResult {
    std::vector<IdentifiedArtwork> images;
    size_t missing = 0;
    std::wstring error;
};
// RAWG sometimes omits a franchise banner that SGDB includes before a colon.
// A known release year (RAWG) picks the one of several same-titled games released then.
GridSource matchIdentifiedArtwork(std::wstring_view title, const std::vector<GridGame>& candidates, int year = 0);
// Worker only. Fill empty/corrupt slots, preserve valid images, persist the confirmed
// source and each downloaded variant. Steam AppID is borrowed only for artwork.
IdentifiedArtworkResult fillIdentifiedArtwork(const Game& game, const std::wstring& title, uint32_t steam_app_id,
    const std::filesystem::path& cache, const std::wstring& key, const std::function<bool()>& active,
    const ArtworkHttp& get = httpGet, int year = 0);
