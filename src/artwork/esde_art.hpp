#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class EmuArtKind { cover, background, logo };

// Read local artwork already scraped by ES-DE/ScreenScraper, never for Steam apps. The game
// is found in gamelist.xml by its ROM file (library/esde_gamelist.hpp), else by title.
std::vector<uint8_t> esdeArtForShortcut(uint32_t app_id, const std::wstring& platform,
                                        const std::wstring& title, EmuArtKind kind,
                                        const std::filesystem::path& cache,
                                        const std::filesystem::path& rom = {});
std::vector<uint8_t> esdeArtFromMedia(const std::filesystem::path& media_root,
                                      const std::filesystem::path& gamelists_root,
                                      const std::wstring& system, const std::wstring& title,
                                      EmuArtKind kind, const std::filesystem::path& rom = {});
// "Zmień grafikę" → Źródło: ES-DE. The game's files in the given downloaded_media folders
// ("covers", "fanart", ...), found like esdeArtForShortcut (ROM file, else title).
std::vector<std::filesystem::path> esdeMediaFiles(const std::wstring& platform, const std::wstring& title,
                                                  const std::filesystem::path& rom, const std::vector<const wchar_t*>& folders);
// ES-DE's downloaded video of the game (downloaded_media/<system>/videos/<rom stem>.mp4 ...),
// only for a game found in its gamelist; empty when there is none.
std::filesystem::path esdeVideoFile(const std::wstring& platform, const std::wstring& title, const std::filesystem::path& rom);
// <cache>/(covers|backgrounds|logos)/<app_id>.img: the copy esdeArtForShortcut keeps.
std::filesystem::path esdeArtCachePath(uint32_t app_id, EmuArtKind kind, const std::filesystem::path& cache);
