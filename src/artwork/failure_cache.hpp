#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace artwork_failure {
uint64_t now();
uint64_t blockedUntil(const std::filesystem::path& root, std::string_view key, std::wstring_view title, uint64_t time);
bool record(const std::filesystem::path& root, std::string_view key, std::wstring_view title,
            uint64_t request_started, uint64_t until, bool transient);
bool retry(const std::filesystem::path& root, std::string_view key, uint64_t time);
void clear(const std::filesystem::path& root, std::string_view key);
}
