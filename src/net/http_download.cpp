#include "http_download.hpp"
#include "winhttp_session.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace {
using Internet = winhttp::Handle;
std::wstring header(HINTERNET request, DWORD field) {
    std::array<wchar_t, 16384> value{}; DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    return WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size, WINHTTP_NO_HEADER_INDEX) ? std::wstring(value.data(), size / sizeof(wchar_t)) : std::wstring{};
}
std::optional<uint64_t> number(std::wstring_view value) {
    if (value.empty() || value.size() > 20 || value.find_first_not_of(L"0123456789") != value.npos) return {};
    std::string text; for (const auto c : value) text += static_cast<char>(c);
    uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional{result} : std::nullopt;
}
}
bool validDownloadRange(std::wstring_view value, uint64_t offset, uint64_t total) {
    if (!value.starts_with(L"bytes ") || !total || offset >= total) return false;
    value.remove_prefix(6); const auto dash = value.find(L'-'), slash = value.find(L'/');
    if (dash == value.npos || slash == value.npos || dash >= slash) return false;
    return number(value.substr(0, dash)) == offset && number(value.substr(dash + 1, slash - dash - 1)) == total - 1 && number(value.substr(slash + 1)) == total;
}
DownloadError httpDownload(const std::wstring& original_url, const std::filesystem::path& partial, uint64_t total,
    const std::function<bool()>& cancel, const std::function<void(uint64_t)>& progress,
    const std::function<bool(std::wstring_view)>& allowed_url) {
    if (!total || !allowed_url(original_url)) return DownloadError::invalid_response;
    struct File { HANDLE value; ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } file{
        CreateFileW(partial.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return DownloadError::storage;
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER length{};
    if (!GetFileInformationByHandle(file.value, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || !GetFileSizeEx(file.value, &length)) return DownloadError::storage;
    uint64_t offset = static_cast<uint64_t>(length.QuadPart);
    if (offset > total) return DownloadError::invalid_response;
    if (offset == total) { progress(offset); return DownloadError::none; }
    const auto session = winhttp::session();
    if (!session) return DownloadError::network;
    std::vector<uint8_t> bytes(256 * 1024);
    for (int attempt = 0; attempt < 3 && !cancel(); ++attempt) {
        uint32_t retry_seconds = 1u << attempt;
        auto url = original_url;
        for (int redirects = 0; redirects <= 3 && !cancel(); ++redirects) {
            if (!allowed_url(url)) return DownloadError::invalid_response;
            URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return DownloadError::invalid_response;
            Internet connection{WinHttpConnect(session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0)};
            if (!connection.value) break;
            std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
            Internet request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
            if (!request.value) break;
            WinHttpSetTimeouts(request.value, 5000, 5000, 5000, 10000);
            winhttp::isolate(request.value);
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
            const auto headers = L"Accept-Encoding: identity\r\n" + (offset ? L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n" : std::wstring{});
            if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                cancel() || !WinHttpReceiveResponse(request.value, nullptr)) break;
            DWORD status = 0, size = sizeof(status);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) break;
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                url = header(request.value, WINHTTP_QUERY_LOCATION); continue;
            }
            if (status == 429 || status == 503) {
                if (const auto seconds = number(header(request.value, WINHTTP_QUERY_RETRY_AFTER))) {
                    if (*seconds > 60) return DownloadError::network;
                    retry_seconds = static_cast<uint32_t>(std::max<uint64_t>(1, *seconds));
                }
                break;
            }
            if (status >= 500) break;
            if (status != 200 && status != 206) return DownloadError::network;
            if (status == 206 && !validDownloadRange(header(request.value, WINHTTP_QUERY_CONTENT_RANGE), offset, total)) return DownloadError::invalid_response;
            const auto content_length = number(header(request.value, WINHTTP_QUERY_CONTENT_LENGTH));
            const uint64_t begin = status == 200 ? 0 : offset;
            if (!content_length || *content_length != total - begin) return DownloadError::invalid_response;
            LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(begin);
            if (!SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN) || (status == 200 && !SetEndOfFile(file.value))) return DownloadError::storage;
            offset = begin; progress(offset);
            bool finished = false;
            while (!cancel()) {
                DWORD read = 0;
                if (!WinHttpReadData(request.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read)) break;
                if (!read) { finished = offset == total; break; }
                if (read > total - offset) return DownloadError::invalid_response;
                DWORD written = 0;
                if (!WriteFile(file.value, bytes.data(), read, &written, nullptr) || written != read) return DownloadError::storage;
                offset += read; progress(offset);
            }
            if (!FlushFileBuffers(file.value)) return DownloadError::storage;
            if (finished || (cancel() && offset <= total)) return DownloadError::none;
            break;
        }
        if (attempt < 2) for (uint32_t tick = 0; tick < retry_seconds * 10 && !cancel(); ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return cancel() ? DownloadError::none : DownloadError::network;
}
