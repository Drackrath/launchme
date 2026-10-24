#include "http.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "winhttp_session.hpp"
#include <shlwapi.h>
#include "artwork_limits.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <thread>

namespace {
thread_local ULONGLONG worker_deadline = std::numeric_limits<ULONGLONG>::max();
thread_local const std::atomic_bool* worker_cancelled = nullptr;
std::mutex sgdb_mutex;
ArtworkLimit sgdb_limit;
using winhttp::Handle;
} // namespace

HttpBudget::HttpBudget(uint32_t milliseconds) : previous_(worker_deadline) {
    worker_deadline = std::min(worker_deadline, GetTickCount64() + milliseconds);
}
HttpBudget::~HttpBudget() { worker_deadline = previous_; }
HttpCancellation::HttpCancellation(const std::atomic_bool& cancelled) : previous_(worker_cancelled) { worker_cancelled = &cancelled; }
HttpCancellation::~HttpCancellation() { worker_cancelled = previous_; }
bool httpCancelled() { return worker_cancelled && worker_cancelled->load(); }
uint32_t steamGridDbWaitSeconds() {
    std::lock_guard lock(sgdb_mutex);
    const auto now = GetTickCount64();
    return sgdb_limit.blocked > now ? static_cast<uint32_t>((sgdb_limit.blocked - now + 999) / 1000) : 0;
}

bool lanHost(std::wstring_view host) {
    if (host.empty()) return false;
    if (host == L"127.0.0.1" || _wcsicmp(std::wstring(host).c_str(), L"localhost") == 0) return true;
    // Private IPv4 literals: 10/8, 172.16/12, 192.168/16, 169.254/16.
    int parts[4]{}, count = 0;
    size_t at = 0;
    for (; count < 4 && at <= host.size(); ++count) {
        const auto dot = host.find(L'.', at);
        const auto piece = host.substr(at, dot == std::wstring_view::npos ? std::wstring_view::npos : dot - at);
        if (piece.empty() || piece.size() > 3 || !std::all_of(piece.begin(), piece.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) { count = -1; break; }
        parts[count] = std::stoi(std::wstring(piece));
        if (parts[count] > 255) { count = -1; break; }
        if (dot == std::wstring_view::npos) { ++count; break; }
        at = dot + 1;
    }
    if (count == 4) return parts[0] == 10 || parts[0] == 127 || (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) ||
                           (parts[0] == 192 && parts[1] == 168) || (parts[0] == 169 && parts[1] == 254);
    if (count == -1 && host.find(L'.') != std::wstring_view::npos && host.find_first_not_of(L"0123456789.") == std::wstring_view::npos) return false;
    // A machine's name on the home network: one label ("nas") or .local / .lan / .home.arpa.
    std::wstring lower(host);
    for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    return lower.find(L'.') == std::wstring::npos || lower.ends_with(L".local") || lower.ends_with(L".lan") || lower.ends_with(L".home.arpa");
}

namespace {
bool publicAddress(const sockaddr* address) {
    if (address->sa_family == AF_INET) {
        const auto ip = ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr);
        const auto a = ip >> 24, b = (ip >> 16) & 255;
        return a != 0 && a != 10 && a != 127 && a < 224 && !(a == 169 && b == 254) &&
               !(a == 172 && b >= 16 && b <= 31) && !(a == 192 && (b == 168 || b == 0)) &&
               !(a == 100 && b >= 64 && b <= 127) && !(a == 198 && (b == 18 || b == 19));
    }
    if (address->sa_family == AF_INET6) {
        const auto& ip = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr;
        // Global unicast only; mapped IPv4, local, multicast and translation prefixes excluded.
        return (ip.u.Byte[0] & 0xe0) == 0x20;
    }
    return false;
}
bool pageHost(const std::wstring& url, std::wstring& host) {
    if (url.empty() || url.size() > 2048 || url.find_first_of(L"\r\n\t\\") != url.npos) return false;
    URL_COMPONENTS p{sizeof(p)};
    p.dwHostNameLength = p.dwUserNameLength = p.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &p) || !p.dwHostNameLength ||
        p.dwUserNameLength || p.dwPasswordLength ||
        (p.nScheme != INTERNET_SCHEME_HTTP && p.nScheme != INTERNET_SCHEME_HTTPS) ||
        (p.nPort != 80 && p.nPort != 443)) return false;
    host.assign(p.lpszHostName, p.dwHostNameLength);
    for (auto& ch : host) ch = static_cast<wchar_t>(std::towlower(ch));
    while (host.ends_with(L".")) host.pop_back();
    if (host.empty() || lanHost(host) || host.ends_with(L".localhost") || host.ends_with(L".internal") ||
        host.ends_with(L".invalid")) return false;
    if (host.front() == L'[' && host.back() == L']') host = host.substr(1, host.size() - 2);
    sockaddr_in v4{}; v4.sin_family = AF_INET;
    if (InetPtonW(AF_INET, host.c_str(), &v4.sin_addr) == 1) return publicAddress(reinterpret_cast<sockaddr*>(&v4));
    sockaddr_in6 v6{}; v6.sin6_family = AF_INET6;
    if (InetPtonW(AF_INET6, host.c_str(), &v6.sin6_addr) == 1) return publicAddress(reinterpret_cast<sockaddr*>(&v6));
    return host.find_first_not_of(L"0123456789.") != host.npos && host.find(L':') == host.npos;
}
bool resolvesPublicly(const std::wstring& host) {
    static const bool initialized = [] { WSADATA data{}; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    if (!initialized) return false;
    ADDRINFOEXW hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    ADDRINFOEXW* addresses = nullptr;
    OVERLAPPED operation{}; operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) return false;
    HANDLE cancel = nullptr;
    int error = GetAddrInfoExW(host.c_str(), nullptr, NS_DNS, nullptr, &hints, &addresses, nullptr, &operation, nullptr, &cancel);
    if (error == WSA_IO_PENDING) {
        const auto deadline = std::min(worker_deadline, GetTickCount64() + 3000);
        while (WaitForSingleObject(operation.hEvent, 25) == WAIT_TIMEOUT) {
            if (httpCancelled() || GetTickCount64() >= deadline) {
                GetAddrInfoExCancel(&cancel);
                // The overlapped storage must outlive completion, including cancellation.
                WaitForSingleObject(operation.hEvent, INFINITE);
                break;
            }
        }
        error = GetAddrInfoExOverlappedResult(&operation);
    }
    CloseHandle(operation.hEvent);
    bool ok = !error && addresses != nullptr && !httpCancelled();
    for (auto* a = addresses; a; a = a->ai_next) if (!publicAddress(a->ai_addr)) ok = false;
    if (addresses) FreeAddrInfoExW(addresses);
    return ok;
}
}

bool publicWebUrl(const std::wstring& url) { std::wstring host; return pageHost(url, host); }

// sink: streamed bodies (downloads to a file) instead of response.body; false aborts.
// plain: 0 HTTPS only, 1 also HTTP to this machine, 2 also HTTP to the home network (lanHost).
// live: hand the sink each piece as soon as it arrives (server-sent events), not a full buffer.
// refusals: a 4xx answer keeps up to 64 KiB of its body (an API's JSON reason).
static HttpResponse requestHttp(const std::wstring& url, const std::wstring& headers, size_t max_bytes, bool private_request, const std::string* body = nullptr,
                                uint32_t cap_ms = 20000, const std::function<bool(uint32_t status, const uint8_t*, size_t)>& sink = {},
                                int receive_ms = 8000, int plain = 0, bool live = false, bool refusals = false, bool page = false) {
    const auto deadline = std::min(worker_deadline, GetTickCount64() + cap_ms);
    const auto remaining = [&]() -> int { const auto now = GetTickCount64(); return httpCancelled() || now >= deadline ? 0 : static_cast<int>(deadline - now); };
    if (!remaining()) return {};
    if (page) {
        std::wstring host;
        if (!pageHost(url, host) || !resolvesPublicly(host) || !remaining()) return {};
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return {};
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    // Plain HTTP only to this machine (local engines) or, for services the user named, the home
    // network; everything else is HTTPS.
    const bool loopback = host == L"127.0.0.1" || _wcsicmp(host.c_str(), L"localhost") == 0;
    const bool local = parts.nScheme == INTERNET_SCHEME_HTTP && ((plain >= 1 && loopback) || (plain >= 2 && lanHost(host)));
    const bool insecure = local || (page && parts.nScheme == INTERNET_SCHEME_HTTP);
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && !insecure) return {};
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool sgdb = (_wcsicmp(host.c_str(), L"www.steamgriddb.com") == 0 || _wcsicmp(host.c_str(), L"steamgriddb.com") == 0) && path.starts_with(L"/api/");
    if (sgdb) for (;;) {
        if (!remaining()) return {};
        uint64_t wait = 0;
        {
            std::lock_guard lock(sgdb_mutex);
            const auto now = GetTickCount64();
            if (sgdb_limit.blocked > now) return {429, {}, static_cast<uint32_t>((sgdb_limit.blocked - now + 999) / 1000)};
            wait = sgdb_limit.delay(now);
            if (!wait) { sgdb_limit.reserve(now); break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint64_t>(wait, 100)));
    }
    const auto session = winhttp::session();
    if (!session) return {};
    Handle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connection) return {};
    Handle request(WinHttpOpenRequest(connection.value, body ? L"POST" : L"GET", path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, insecure ? 0 : WINHTTP_FLAG_SECURE));
    if (!request) return {};
    if (local) { // Never through a proxy.
        WINHTTP_PROXY_INFO direct{WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr};
        WinHttpSetOption(request.value, WINHTTP_OPTION_PROXY, &direct, sizeof(direct));
    }
    // Timeouts belong to the request now that the session is shared by every worker.
    const int phase = std::max(1, remaining() / 4);
    WinHttpSetTimeouts(request.value, std::min(3000, phase), std::min(3000, phase), std::min(5000, phase), receive_ms <= 8000 ? std::min(receive_ms, phase) : std::min(receive_ms, remaining()));
    winhttp::isolate(request.value);
    if (page) {
        DWORD auth = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &auth, sizeof(auth));
    }
    DWORD redirect_limit = 3;
    WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirect_limit, sizeof(redirect_limit));
    if (private_request || headers.find(L"Authorization:") != std::wstring::npos) {
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    }
    if (body && body->size() > MAXDWORD) return {};
    if (!WinHttpSendRequest(request.value, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1),
                            body ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA,
                            body ? static_cast<DWORD>(body->size()) : 0, body ? static_cast<DWORD>(body->size()) : 0, 0) || !remaining()) return {};
    DWORD receive_timeout = static_cast<DWORD>(std::min(receive_ms, remaining()));
    if (!receive_timeout) return {};
    WinHttpSetOption(request.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receive_timeout, sizeof(receive_timeout));
    if (!WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) return {};
    HttpResponse response{status};
    if (page) {
        const auto header = [&](DWORD kind) {
            wchar_t text[4096]{}; DWORD bytes = sizeof(text);
            return WinHttpQueryHeaders(request.value, kind, WINHTTP_HEADER_NAME_BY_INDEX, text, &bytes, WINHTTP_NO_HEADER_INDEX)
                ? std::wstring(text) : std::wstring{};
        };
        response.content_type = header(WINHTTP_QUERY_CONTENT_TYPE);
        response.location = header(WINHTTP_QUERY_LOCATION);
        auto type = response.content_type;
        for (auto& ch : type) ch = static_cast<wchar_t>(std::towlower(ch));
        if (status == 200 && !type.starts_with(L"text/html") && !type.starts_with(L"application/xhtml+xml") &&
            !type.starts_with(L"text/plain") && !type.starts_with(L"application/json")) return response;
    }
    if (status == 429 || status == 503) {
        wchar_t retry[96]{}; DWORD size = sizeof(retry);
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, retry, &size, WINHTTP_NO_HEADER_INDEX)) {
            wchar_t* end = nullptr;
            const auto seconds = std::wcstoul(retry, &end, 10);
            if (end != retry && !*end) response.retry_after_seconds = static_cast<uint32_t>(std::min<unsigned long>(seconds, 7 * 86400));
            else {
                SYSTEMTIME time{}; FILETIME target{}, current{};
                if (WinHttpTimeToSystemTime(retry, &time) && SystemTimeToFileTime(&time, &target)) {
                    GetSystemTimeAsFileTime(&current);
                    const auto ticks = [](FILETIME f) { return (uint64_t{f.dwHighDateTime} << 32) | f.dwLowDateTime; };
                    if (ticks(target) > ticks(current)) response.retry_after_seconds = static_cast<uint32_t>(std::min<uint64_t>(7 * 86400, (ticks(target) - ticks(current) + 9999999) / 10000000));
                }
            }
        }
    }
    if (sgdb) {
        const auto header = [&](const wchar_t* name) -> std::optional<uint64_t> {
            wchar_t text[96]{}; DWORD size = sizeof(text);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CUSTOM, name, text, &size, WINHTTP_NO_HEADER_INDEX)) return {};
            wchar_t* end = nullptr; const auto n = _wcstoui64(text, &end, 10);
            return end != text && !*end ? std::optional<uint64_t>(n) : std::nullopt;
        };
        const auto reset = header(L"X-RateLimit-Reset");
        const auto now_epoch = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const uint64_t reset_ms = reset && *reset > now_epoch ? std::min<uint64_t>(*reset - now_epoch, 7 * 86400) * 1000 : 0;
        std::lock_guard lock(sgdb_mutex);
        sgdb_limit.observe(GetTickCount64(), status, uint64_t{response.retry_after_seconds} * 1000, header(L"X-RateLimit-Remaining"), reset_ms);
    }
    const bool refusal = refusals && !sink && status >= 400 && status < 500;
    if (status != 200 && !(sink && status == 206) && !refusal) return response;
    if (refusal) max_bytes = std::min<size_t>(max_bytes, 64 * 1024);
    uint8_t buffer[16384];
    size_t total = 0;
    while (true) {
        if (!remaining()) return {};
        receive_timeout = static_cast<DWORD>(std::min(receive_ms, remaining()));
        if (!receive_timeout) return {};
        WinHttpSetOption(request.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receive_timeout, sizeof(receive_timeout));
        DWORD received = 0;
        if (live) { // Whatever has arrived, at once (waits only while nothing has).
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available)) return {};
            if (!available) break;
            if (!WinHttpReadData(request.value, buffer, std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer))), &received)) return {};
        } else if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received)) return {};
        if (!received) break;
        total += received;
        if (page && total > max_bytes) {
            response.body.insert(response.body.end(), buffer, buffer + (max_bytes - response.body.size()));
            response.truncated = true;
            break;
        }
        if (total > max_bytes) { if (refusal) break; return {}; } // A refusal keeps its status and what came.
        if (sink) { if (!sink(status, buffer, received)) return {}; }
        else response.body.insert(response.body.end(), buffer, buffer + received);
    }
    return response;
}

HttpResponse httpGet(const std::wstring& url, const std::wstring& headers, size_t max_bytes) { return requestHttp(url, headers, max_bytes, false); }
HttpResponse httpGetPage(const std::wstring& url, size_t max_bytes) {
    HttpBudget budget(12000);
    auto current = url;
    for (int hop = 0; hop < 4 && !httpCancelled(); ++hop) {
        auto response = requestHttp(current, L"Accept: text/html, application/xhtml+xml, application/json, text/plain\r\n", max_bytes, true,
                                    nullptr, 12000, {}, 4000, 0, false, false, true);
        if (response.status != 301 && response.status != 302 && response.status != 303 && response.status != 307 && response.status != 308) return response;
        if (response.location.empty()) return response;
        wchar_t combined[4096]{}; DWORD count = static_cast<DWORD>(std::size(combined));
        if (FAILED(UrlCombineW(current.c_str(), response.location.c_str(), combined, &count, 0))) return {};
        current = combined; // Next iteration revalidates every destination before requesting it.
    }
    return {};
}
HttpResponse httpGetPrivate(const std::wstring& url, const std::wstring& headers, size_t max_bytes) { return requestHttp(url, headers, max_bytes, true); }
HttpResponse httpPostPrivate(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes) { return requestHttp(url, headers, max_bytes, true, &body); }
HttpResponse httpPostLong(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms) {
    return requestHttp(url, headers, max_bytes, true, &body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)));
}

HttpResponse httpPostStream(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms,
                            const std::function<void(const uint8_t*, size_t)>& piece) {
    std::vector<uint8_t> received;
    auto response = requestHttp(url, headers, max_bytes, true, &body, timeout_ms, [&](uint32_t, const uint8_t* data, size_t size) {
        received.insert(received.end(), data, data + size);
        if (piece) piece(data, size);
        return true;
    }, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 0, true);
    if (response.status == 200) response.body = std::move(received);
    return response;
}

HttpResponse httpLocal(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return requestHttp(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 1);
}

HttpResponse httpService(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return requestHttp(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 180000)), 2);
}

HttpResponse httpApi(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return requestHttp(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 0, false, true);
}

HttpResponse httpDownloadFile(const std::wstring& url, const std::wstring& headers, const std::filesystem::path& file,
                              size_t max_bytes, uint32_t timeout_ms, bool private_request) {
    // <file>.part keeps what arrived; the next call asks for the rest (Range) and appends.
    auto part = file; part += L".part";
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    uint64_t have = std::filesystem::is_regular_file(part, ec) ? std::filesystem::file_size(part, ec) : 0;
    if (ec) have = 0;
    std::wstring request_headers = headers;
    if (have) request_headers += L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
    std::ofstream output;
    bool opened = false;
    HttpResponse response;
    {
        HttpBudget budget(timeout_ms);
        response = requestHttp(url, request_headers, max_bytes, private_request, nullptr, timeout_ms,
            [&](uint32_t status, const uint8_t* data, size_t size) {
                if (!opened) { // 206 continues the part file, 200 (Range ignored) starts it over.
                    output.open(part, std::ios::binary | (status == 206 ? std::ios::app : std::ios::trunc));
                    opened = output.is_open();
                    if (!opened) return false;
                }
                output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                return output.good();
            });
    }
    const bool written = opened && output.good();
    if (opened) output.close();
    if ((response.status == 200 || response.status == 206) && written &&
        MoveFileExW(part.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {200};
    if (response.status == 416) std::filesystem::remove(part, ec); // A part that no longer fits: start over next time.
    return {response.status == 200 || response.status == 206 ? 0u : response.status};
}
