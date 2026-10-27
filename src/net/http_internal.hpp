#pragma once
// Shared between http.cpp (portable rules, the public calls) and the platform transport that
// performs a request: http_winhttp.cpp (Windows) or http_curl.cpp (elsewhere).
#include "http.hpp"
#include "artwork_limits.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace http_detail {
// Milliseconds on a monotonic clock.
uint64_t now();
// The current worker's deadline (HttpBudget), as now() milliseconds.
uint64_t workerDeadline();

// SteamGridDB's API budget, shared by every worker.
std::mutex& steamGridDbMutex();
ArtworkLimit& steamGridDbLimit();
bool steamGridDbApi(std::wstring_view host, std::wstring_view path);

// A public IPv4/IPv6 address (sockaddr*): never this machine, the home network, link-local,
// carrier-grade NAT, benchmarking, multicast or translation ranges.
bool publicAddress(const void* address);

// Called before each redirect hop of httpGetPage: `location` resolved against `base`, empty when
// it cannot be.
std::wstring combineUrl(const std::wstring& base, const std::wstring& location);

// One request on the platform transport.
// sink: streamed bodies (downloads to a file) instead of response.body; false aborts.
// plain: 0 HTTPS only, 1 also HTTP to this machine, 2 also HTTP to the home network (lanHost).
// live: hand the sink each piece as soon as it arrives (server-sent events), not a full buffer.
// refusals: a 4xx answer keeps up to 64 KiB of its body (an API's JSON reason).
// page: a public link preview (publicWebUrl, resolved to public addresses only, textual bodies;
// a long page keeps its bounded prefix).
HttpResponse request(const std::wstring& url, const std::wstring& headers, size_t max_bytes, bool private_request,
                     const std::string* body = nullptr, uint32_t cap_ms = 20000,
                     const std::function<bool(uint32_t status, const uint8_t*, size_t)>& sink = {},
                     int receive_ms = 8000, int plain = 0, bool live = false, bool refusals = false, bool page = false);
}
