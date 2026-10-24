#pragma once
// Helpers shared by the Launcher translation units (src/app/*.cpp). Not part of the
// Launcher interface; main.cpp and the tests never include this header.
#include "launcher.hpp"
#include "../artwork/platform_logo.hpp"

#include <string>

// Holding Enter at least this long on a game opens its context menu instead of launching.
inline constexpr float enter_hold_seconds = 0.48f;

// One key per artwork request, shared by the UI-side queue bookkeeping and the download
// workers. Platform badges are keyed by logo file, so games of one platform share one fetch.
inline std::string artRequestKey(const Game& game, CoverMessage::Kind kind) {
    if (kind == CoverMessage::Kind::badge) {
        const auto filename = platformLogoFilename(activePlatformName(game));
        std::string key = "badge:";
        for (wchar_t ch : filename) key += static_cast<char>(ch); // Fixed ASCII filenames.
        return key;
    }
    return gameKey(game) + ':' + std::to_string(static_cast<int>(kind));
}

// The cached file kind behind a game artwork request (badges are not game artwork).
inline ArtworkKind artworkKind(CoverMessage::Kind kind) {
    switch (kind) {
    case CoverMessage::Kind::logo: return ArtworkKind::logo;
    case CoverMessage::Kind::hero: return ArtworkKind::hero;
    case CoverMessage::Kind::poster: return ArtworkKind::poster;
    case CoverMessage::Kind::landscape: return ArtworkKind::landscape;
    case CoverMessage::Kind::disc: return ArtworkKind::disc;
    default: return ArtworkKind::cover;
    }
}
