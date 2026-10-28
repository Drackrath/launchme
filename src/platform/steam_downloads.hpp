#pragma once
#include "../model/installation.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <unordered_map>

struct DownloadBytes { uint64_t done = 0, total = 0; InstallPhase phase = InstallPhase::none; };
// The private AppManager ABI is never called blindly. Steam updates its client every week or
// two, so instead of a list of tested builds the loaded DLL itself is checked: each method of
// the IPC proxy references its own name ("GetUpdateInfo", ...) for logging, so a vtable slot
// is used only when its function loads exactly that name. A build where the names cannot be
// matched falls back to the public counters; nothing unknown is called. Verified 2026-09-28
// (PE timestamp 1790379349) and 2026-10-04 (1790903896): GetUpdateInfo 20, BIsDownloadingEnabled 40.
//
// True when x64 `code` (loaded at `code_rva`) holds a RIP-relative LEA of `target_rva`.
inline bool referencesRva(std::span<const uint8_t> code, uint32_t code_rva, uint32_t target_rva) {
    for (size_t i = 0; i + 7 <= code.size(); ++i) {
        // REX.W or REX.WR, 8D (LEA), ModRM mod=00 rm=101: [rip + disp32].
        if ((code[i] & 0xFB) != 0x48 || code[i + 1] != 0x8D || (code[i + 2] & 0xC7) != 0x05) continue;
        int32_t displacement = 0; memcpy(&displacement, code.data() + i + 3, 4);
        if (static_cast<int64_t>(code_rva) + static_cast<int64_t>(i) + 7 + displacement == target_rva) return true;
    }
    return false;
}
inline std::optional<DownloadBytes> steamUpdateCounters(std::span<const uint8_t> bytes) {
    if (bytes.size() < 80) return {};
    uint32_t state = 0; memcpy(&state, bytes.data() + 4, 4);
    if (!(state & 1)) return {};
    DownloadBytes result;
    if (state & 32) {
        result.phase = InstallPhase::downloading;
        memcpy(&result.total, bytes.data() + 64, 8); memcpy(&result.done, bytes.data() + 72, 8);
        if (!result.total || result.total > (uint64_t{1} << 50) || result.done > result.total) return {};
    } else result.phase = state & 4 ? InstallPhase::verifying : state & (64 | 128) ? InstallPhase::staging : InstallPhase::allocating;
    return result;
}
// Linux has no AppManager reads: Steam's logs/content_log.txt is followed instead. Per app it
// keeps the last update phase ("App update changed : Running Update,Downloading,Staging,"),
// the counters logged when an update starts or resumes ("update started : download a/b, ...,
// stage c/d") and a pause ("state changed : ...Update Paused..." or "(Suspended)"). The
// counters are only a starting point; appmanifest_<app>.acf has the current ones.
class SteamContentLog {
public:
    void feed(std::string_view text); // Whole lines; a trailing partial line is ignored.
    std::optional<DownloadBytes> state(uint32_t app) const;
    void clear() { apps_.clear(); }
private:
    struct App { InstallPhase phase = InstallPhase::none; bool paused = false; uint64_t download_done = 0, download_total = 0, stage_done = 0, stage_total = 0; };
    std::unordered_map<uint32_t, App> apps_;
};
// The BytesDownloaded/BytesToDownload (or, for staging, BytesStaged/BytesToStage) of an ACF.
DownloadBytes steamManifestCounters(std::string_view acf, bool staging);
// Worker-only read-only IPC (Windows; on Linux the files above, and connected() means the log is readable). No SteamAPI_Init/AppID impersonation, injection,
// installation commands, or callbacks on the frame/UI thread.
class SteamDownloads {
public:
    explicit SteamDownloads(std::filesystem::path root);
    ~SteamDownloads();
    SteamDownloads(const SteamDownloads&) = delete;
    SteamDownloads& operator=(const SteamDownloads&) = delete;
    std::optional<DownloadBytes> read(uint32_t app);
    bool connected() const;
    bool appManagerConnected() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
inline void applyLiveDownload(Installation& installation, std::optional<DownloadBytes> bytes) {
    if (!installation.active()) return;
    if (bytes && bytes->phase != InstallPhase::none) {
        if (installation.phase != InstallPhase::paused) installation.phase = bytes->phase;
        installation.done = bytes->done; installation.total = bytes->total; installation.live = true;
        return;
    }
    if (bytes && bytes->total && bytes->done < bytes->total && installation.phase == InstallPhase::staging)
        installation.phase = InstallPhase::downloading; // Steam can download and stage concurrently.
    if (bytes && bytes->total && bytes->done <= bytes->total &&
        (installation.phase == InstallPhase::downloading || installation.phase == InstallPhase::paused || installation.phase == InstallPhase::queued)) {
        installation.done = bytes->done; installation.total = bytes->total; installation.live = true;
    } else if (!installation.done) {
        installation.total = 0; // An ACF's stale zero is not a live progress sample.
    }
}
