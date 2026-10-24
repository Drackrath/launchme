#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

std::wstring platformLogoFilename(const std::wstring& platform);
// Cache/request identity for the compact package mark; other UI keeps its usual platform logo.
std::wstring platformCaseLogoKey(const std::wstring& platform);
// The DRM-free tile's mark (open padlock + "DRM-FREE"), drawn locally as a white PNG.
std::vector<uint8_t> drmFreeLogo();
std::vector<uint8_t> platformLogo(const std::wstring& platform,
                                  const std::filesystem::path& cache);
// Disk only (no download): the logo already in the cache, or the drawn DRM-free mark.
std::vector<uint8_t> platformLogoCached(const std::wstring& platform,
                                        const std::filesystem::path& cache);
// User-supplied platform backdrop (JPEG/PNG). Falls back to the platform's PNG logo.
std::vector<uint8_t> platformBackground(const std::wstring& platform,
                                        const std::filesystem::path& cache,
                                        bool& logo_fallback);
// Disk only: the user's backdrop, else the cached logo (logo_fallback set).
std::vector<uint8_t> platformBackgroundCached(const std::wstring& platform,
                                              const std::filesystem::path& cache,
                                              bool& logo_fallback);
