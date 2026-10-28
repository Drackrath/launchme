#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

enum class DownloadError { none, network, storage, invalid_response };
// A bounded-memory HTTPS stream. A .part is deliberately non-atomic/resumable;
// its caller validates integrity before atomically publishing the final file.
DownloadError httpDownload(const std::wstring& url, const std::filesystem::path& partial, uint64_t total,
    const std::function<bool()>& cancel, const std::function<void(uint64_t)>& progress,
    const std::function<bool(std::wstring_view)>& allowed_url);
bool validDownloadRange(std::wstring_view header, uint64_t offset, uint64_t total);
// A header's plain decimal value (Content-Length, Retry-After seconds); nullopt otherwise.
std::optional<uint64_t> downloadNumber(std::wstring_view value);
