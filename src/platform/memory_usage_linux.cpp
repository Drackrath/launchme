// Linux side of memory_usage.hpp: /proc/self/status and the DRM clients' fdinfo
// (Documentation/gpu/drm-usage-stats.rst). memory_usage.cpp is the Windows one.
#include "memory_usage.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
// Each "key:  value" line of a /proc text file.
template<class Visit> void fields(std::string_view text, Visit visit) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) visit(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }
}
// "1234 kB" / "56 KiB" / "7 MiB" / "89" (bytes) -> bytes; empty when malformed.
std::optional<size_t> bytes(std::string_view value) {
    uint64_t number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end == value.data()) return std::nullopt;
    const auto unit = trim(value.substr(static_cast<size_t>(end - value.data())));
    uint64_t scale = 1;
    if (unit == "kB" || unit == "KiB") scale = uint64_t{1} << 10;
    else if (unit == "MiB") scale = uint64_t{1} << 20;
    else if (unit == "GiB") scale = uint64_t{1} << 30;
    else if (!unit.empty()) return std::nullopt;
    if (number > UINT64_MAX / scale) return std::nullopt;
    return static_cast<size_t>(number * scale);
}
std::string readSmall(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string text;
    if (input) text.assign(std::istreambuf_iterator<char>(input), {});
    return text.size() > 65536 ? std::string{} : text;
}
} // namespace

MemoryUsage memoryFromProcStatus(std::string_view status) {
    MemoryUsage usage;
    size_t anonymous = 0, swapped = 0;
    fields(status, [&](std::string_view key, std::string_view value) {
        const auto size = bytes(value);
        if (!size) return;
        if (key == "VmRSS") usage.working_set = *size;
        else if (key == "RssAnon") anonymous = *size;
        else if (key == "VmSwap") swapped = *size;
    });
    usage.private_bytes = anonymous + swapped;
    return usage;
}

std::optional<DrmClientMemory> drmClientMemory(std::string_view fdinfo) {
    DrmClientMemory client;
    bool identified = false;
    // Per memory region the best key: resident, then the legacy "memory" (resident too), then total.
    std::map<std::string, std::pair<int, size_t>, std::less<>> regions;
    fields(fdinfo, [&](std::string_view key, std::string_view value) {
        if (key == "drm-pdev") client.pdev = std::string(value);
        else if (key == "drm-client-id") {
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), client.client);
            identified = error == std::errc{} && end != value.data();
        }
        for (const auto& [prefix, rank] : {std::pair{std::string_view("drm-resident-"), 0}, {"drm-memory-", 1}, {"drm-total-", 2}}) {
            if (!key.starts_with(prefix)) continue;
            const auto size = bytes(value);
            const auto region = key.substr(prefix.size());
            if (!size || region.empty()) return;
            auto found = regions.find(region);
            if (found == regions.end()) regions.emplace(std::string(region), std::pair{rank, *size});
            else if (rank < found->second.first) found->second = {rank, *size};
            return;
        }
    });
    if (!identified) return std::nullopt;
    for (const auto& [region, value] : regions) {
        if (region == "cpu") continue; // Plain system memory the GPU does not map (amdgpu).
        // vram (amdgpu, xe, nouveau), local0 (i915) is the card's own memory; gtt, system0, "memory"
        // (integrated GPUs) is system memory the GPU uses, what DXGI calls shared.
        const bool local = region.find("vram") != std::string::npos || region.starts_with("local");
        (local ? client.local : client.shared) += value.second;
    }
    return client;
}

MemoryUsage currentMemoryUsage() {
    auto usage = memoryFromProcStatus(readSmall("/proc/self/status"));
    // Every descriptor of one DRM client (dup'ed, or the card and its render node) reports the
    // same client: count each pdev/client-id once.
    std::set<std::pair<std::string, uint64_t>> seen;
    std::error_code error;
    for (std::filesystem::directory_iterator entry("/proc/self/fdinfo", error), end; !error && entry != end; entry.increment(error)) {
        const auto client = drmClientMemory(readSmall(entry->path()));
        if (!client || !seen.emplace(client->pdev, client->client).second) continue;
        usage.gpu_local += client->local;
        usage.gpu_shared += client->shared;
    }
    return usage;
}

void trimProcessMemory() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}
