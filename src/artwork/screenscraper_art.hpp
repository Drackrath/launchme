#pragma once

#include "esde_art.hpp"
#include "../library/screenscraper.hpp"

#include <filesystem>
#include <vector>

// Media of an emulated shortcut ScreenScraper identified (the metadata pass caches the
// identification, src/app/ratings.cpp): cover box-2D, background fanart/screenshot, logo
// wheel. Kept like the ES-DE copies in <root>\emulation\(covers|backgrounds|logos)\<appid>.img.
// No identification lookups here: without a cached answer nothing is requested.
std::vector<uint8_t> screenscraperArt(const Game& game, EmuArtKind kind, const std::filesystem::path& config_root,
                                      const screenscraper::Get& get = httpGetPrivate);

// Where a ScreenScraper video of the game is kept: <folder>\<ES-DE system>\videos\<ROM name>.mp4,
// the layout of ES-DE's downloaded_media. Empty without a ROM or a known system.
std::filesystem::path downloadedVideoPath(const Game& game, const std::filesystem::path& folder);
// The video shown behind an emulated shortcut, in this order: ES-DE's own video, one downloaded
// before (downloadedVideoPath), else ScreenScraper's (video-normalized, then video) for an
// identified ROM, downloaded now. Empty when there is none. Worker only: a download takes seconds.
// Writes the video at url to file (complete) or leaves <file>.part to resume; status 200 = complete.
using VideoDownload = std::function<HttpResponse(const std::wstring& url, const std::filesystem::path& file)>;
HttpResponse downloadVideo(const std::wstring& url, const std::filesystem::path& file); // 96 MiB, 5 min, resumable.
std::filesystem::path gameVideoFile(const Game& game, const std::filesystem::path& config_root,
                                    const std::filesystem::path& video_folder, const VideoDownload& download = downloadVideo);

// "Zmień grafikę" → Źródło: ScreenScraper. Every medium of the given ScreenScraper types for the
// ROM; a game not identified yet is looked up now (one jeuInfos request) and cached.
struct ScreenScraperGallery {
    std::vector<screenscraper::Media> media;
    std::wstring error;
    bool retryable = false;
};
ScreenScraperGallery screenscraperGallery(const Game& game, const std::vector<std::wstring_view>& types,
                                          const std::filesystem::path& config_root, const screenscraper::Get& get = httpGetPrivate);
// A medium by its cached URL; thumbnails ask ScreenScraper for a 400 px wide copy.
std::vector<uint8_t> screenscraperImage(std::wstring_view url, bool thumbnail, const screenscraper::Get& get = httpGetPrivate);

// Optical media only. PCs use ScreenScraper's Windows platform, never a console port.
uint32_t discSystemId(const Game& game);
ScreenScraperGallery screenscraperDiscGallery(const Game& game, const std::filesystem::path& config_root,
                                             const screenscraper::Get& get = httpGetPrivate);
bool discImage(const std::vector<uint8_t>& bytes); // A square transparent scan with an optical-disc silhouette.
struct DiscArtwork { std::vector<uint8_t> bytes; bool retryable = false; };
DiscArtwork discArtwork(const Game& game, const std::filesystem::path& cache, const screenscraper::Get& get = httpGetPrivate);
