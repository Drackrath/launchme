// Linux side of steam_downloads.hpp: Steam's own files, read only. steamclient.so is never
// loaded; the content log and the app manifests are followed instead. steam_downloads.cpp is
// the Windows one.
#include "steam_downloads.hpp"
#include "../library/steam.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string>

namespace {
std::optional<uint64_t> number(std::string_view text) {
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data()) return std::nullopt;
    return value;
}
// "<label> <done>/<total>" inside an "update started" line.
void counters(std::string_view line, std::string_view label, uint64_t& done, uint64_t& total) {
    const auto at = line.find(label);
    if (at == std::string_view::npos) return;
    const auto pair = line.substr(at + label.size());
    const auto slash = pair.find('/');
    if (slash == std::string_view::npos) return;
    const auto first = number(pair.substr(0, slash)), second = number(pair.substr(slash + 1));
    if (first && second && *first <= *second) { done = *first; total = *second; }
}
bool has(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }
} // namespace

void SteamContentLog::feed(std::string_view text) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        if (end == std::string_view::npos) break; // Still being written.
        auto line = text.substr(0, end);
        text.remove_prefix(end + 1);
        // "[2026-10-27 18:59:58] AppID 2217000 update started : download 0/12155200, ..."
        const auto stamp = line.find("] AppID ");
        if (line.empty() || line[0] != '[' || stamp == std::string_view::npos) continue;
        line.remove_prefix(stamp + 8);
        const auto space = line.find(' ');
        const auto app = number(line.substr(0, space));
        if (!app || !*app || *app > UINT32_MAX || space == std::string_view::npos) continue;
        const auto event = line.substr(space + 1);
        auto& state = apps_[static_cast<uint32_t>(*app)];
        if (event.starts_with("update started : ")) {
            state.paused = false;
            counters(event, "download ", state.download_done, state.download_total);
            counters(event, "stage ", state.stage_done, state.stage_total);
        } else if (event.starts_with("App update changed : ")) {
            const auto flags = event.substr(21);
            if (flags.starts_with("None")) state.phase = InstallPhase::none;
            else if (has(flags, "Stopping")) state.phase = InstallPhase::paused;
            else if (has(flags, "Verifying")) state.phase = InstallPhase::verifying;
            else if (has(flags, "Downloading")) state.phase = InstallPhase::downloading;
            else if (has(flags, "Staging") || has(flags, "Committing") || has(flags, "Running Script")) state.phase = InstallPhase::staging;
            else if (has(flags, "Preallocating") || has(flags, "Reconfiguring")) state.phase = InstallPhase::allocating;
            else state.phase = InstallPhase::none; // "Running Update," between steps.
        } else if (event.starts_with("state changed : ")) {
            const auto flags = event.substr(16);
            if (has(flags, "Update Paused") || has(flags, "(Suspended)")) state.paused = true;
            else if (has(flags, "Update Running")) state.paused = false;
            else if (!has(flags, "Update")) state = {}; // Installed (or uninstalled): nothing pending.
        }
    }
}

std::optional<DownloadBytes> SteamContentLog::state(uint32_t app) const {
    const auto found = apps_.find(app);
    if (found == apps_.end()) return std::nullopt;
    const auto& state = found->second;
    DownloadBytes result;
    result.phase = state.paused ? InstallPhase::paused : state.phase;
    const bool staging = result.phase == InstallPhase::staging;
    result.done = staging ? state.stage_done : state.download_done;
    result.total = staging ? state.stage_total : state.download_total;
    if (result.phase == InstallPhase::none && !result.total) return std::nullopt;
    return result;
}

DownloadBytes steamManifestCounters(std::string_view acf, bool staging) {
    DownloadBytes result;
    const std::string_view done_key = staging ? "\"BytesStaged\"" : "\"BytesDownloaded\"";
    const std::string_view total_key = staging ? "\"BytesToStage\"" : "\"BytesToDownload\"";
    while (!acf.empty()) {
        const auto end = acf.find('\n');
        auto line = acf.substr(0, end);
        acf.remove_prefix(end == std::string_view::npos ? acf.size() : end + 1);
        const auto first = line.find('"');
        if (first == std::string_view::npos) continue;
        line.remove_prefix(first);
        const bool is_done = line.starts_with(done_key), is_total = line.starts_with(total_key);
        if (!is_done && !is_total) continue;
        line.remove_prefix((is_done ? done_key : total_key).size());
        const auto open = line.find('"'), close = line.rfind('"');
        if (open == std::string_view::npos || close <= open) continue;
        if (const auto value = number(line.substr(open + 1, close - open - 1))) (is_done ? result.done : result.total) = *value;
    }
    if (result.done > result.total) result = {};
    return result;
}

struct SteamDownloads::Impl {
    std::filesystem::path root;
    SteamContentLog log;
    uint64_t offset = 0;
    bool readable = false;
    std::unordered_map<uint32_t, std::filesystem::path> manifests;
    explicit Impl(std::filesystem::path directory) : root(std::move(directory)) {}

    // Reads what Steam appended since the last call; the first call starts 1 MiB before the end
    // (an update in progress logged its start there), a shorter file was rotated.
    void follow() {
        std::ifstream input(root / "logs" / "content_log.txt", std::ios::binary | std::ios::ate);
        readable = static_cast<bool>(input);
        if (!input) return;
        const auto size = static_cast<uint64_t>(std::max<std::streamoff>(input.tellg(), 0));
        bool skip_partial = false;
        if (size < offset) { log.clear(); offset = 0; }
        if (!offset && size > (uint64_t{1} << 20)) { offset = size - (uint64_t{1} << 20); skip_partial = true; }
        const auto length = std::min<uint64_t>(size - offset, uint64_t{4} << 20);
        if (!length) return;
        std::string text(static_cast<size_t>(length), '\0');
        input.seekg(static_cast<std::streamoff>(offset));
        if (!input.read(text.data(), static_cast<std::streamsize>(length))) return;
        size_t begin = 0;
        if (skip_partial) {
            begin = text.find('\n');
            if (begin == std::string::npos) return;
            ++begin;
        }
        const auto last = text.rfind('\n');
        if (last == std::string::npos || last < begin) return;
        log.feed(std::string_view(text).substr(begin, last + 1 - begin));
        offset += last + 1;
    }

    std::string manifest(uint32_t app) {
        const auto name = "appmanifest_" + std::to_string(app) + ".acf";
        auto found = manifests.find(app);
        if (found == manifests.end()) {
            for (const auto& library : steamLibraryFolders(root)) {
                std::error_code error;
                if (std::filesystem::is_regular_file(library / "steamapps" / name, error)) {
                    found = manifests.emplace(app, library / "steamapps" / name).first;
                    break;
                }
            }
            if (found == manifests.end()) return {};
        }
        std::ifstream input(found->second, std::ios::binary | std::ios::ate);
        if (!input) { manifests.erase(found); return {}; }
        const auto size = input.tellg();
        if (size <= 0 || size > 2 * 1024 * 1024) return {};
        std::string text(static_cast<size_t>(size), '\0');
        input.seekg(0);
        input.read(text.data(), size);
        return text;
    }
};

SteamDownloads::SteamDownloads(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
SteamDownloads::~SteamDownloads() = default;
bool SteamDownloads::connected() const { return impl_->readable; }
bool SteamDownloads::appManagerConnected() const { return false; }
std::optional<DownloadBytes> SteamDownloads::read(uint32_t app) {
    if (!app || !impl_->root.is_absolute()) return {};
    impl_->follow();
    auto result = impl_->log.state(app);
    if (!result) return {};
    // The log's counters date from the update's start; the manifest's are newer unless Steam has
    // not written it yet (its stale zero).
    const auto current = steamManifestCounters(impl_->manifest(app), result->phase == InstallPhase::staging);
    if (current.total && (current.done >= result->done || current.total != result->total)) {
        result->done = current.done; result->total = current.total;
    }
    return result;
}
