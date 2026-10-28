#include "steam_controller.hpp"
#include <hidsdi.h>
#include <xinput.h>
#include <algorithm>
#include <cwctype>
#include <mutex>
#include <thread>
#include <utility>

// Raw Input enumeration and overlapped HID I/O; the parsers are in steam_controller.cpp.
namespace steam_controller {
namespace {
constexpr ULONGLONG rescan_ms = 2000, heartbeat_ms = 2000, steam_check_ms = 2000;
// USB and the Puck stream state at ~64 Hz even when idle; a silent slot is empty or asleep.
constexpr ULONGLONG stale_ms = 1500;

bool receiver(uint16_t product) { return product == 0x1304 || product == 0x1305; }
bool bluetoothPath(std::wstring path, uint16_t product) {
    for (auto& c : path) c = static_cast<wchar_t>(std::towlower(c));
    return product == 0x1303 || path.find(L"{00001812-0000-1000-8000-00805f9b34fb}") != path.npos ||
           path.find(L"bthledevice") != path.npos || path.find(L"bthenum") != path.npos;
}

// Steam keeps its PID here while running; the image name guards against a stale, reused PID.
bool steamRunning() {
    DWORD pid = 0, size = sizeof(pid);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD,
                     nullptr, &pid, &size) != ERROR_SUCCESS || !pid) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    // Long-path installs exceed MAX_PATH; a failed query would read Steam as not running and
    // take the controller Steam owns.
    wchar_t image[32768];
    DWORD length = static_cast<DWORD>(std::size(image));
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(process, 0, image, &length);
    CloseHandle(process);
    if (!alive) return false;
    std::wstring_view name(image, length);
    name = name.substr(name.find_last_of(L"\\/") + 1);
    return CompareStringOrdinal(name.data(), static_cast<int>(name.size()), L"steam.exe", -1, TRUE) == CSTR_EQUAL;
}

struct Interface { std::wstring path; uint16_t product = 0; };
// Raw Input lists every HID top-level collection, including vendor-defined ones.
std::vector<Interface> interfaces() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1) || !count) return {};
    std::vector<RAWINPUTDEVICELIST> list(count + 8); // Room for a device arriving between the calls.
    count = static_cast<UINT>(list.size());
    const UINT listed = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (listed == UINT(-1)) return {};
    std::vector<Interface> result;
    for (UINT i = 0; i < listed; ++i) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{sizeof(info)};
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) continue;
        const auto vendor = static_cast<uint16_t>(info.hid.dwVendorId), product = static_cast<uint16_t>(info.hid.dwProductId);
        if (!supported(vendor, product) || !inputCollection(info.hid.usUsagePage, info.hid.usUsage)) continue;
        UINT length = 0;
        GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, nullptr, &length);
        std::wstring path(length, L'\0');
        if (!length || GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, path.data(), &length) == UINT(-1)) continue;
        path.resize(wcslen(path.c_str()));
        result.push_back({std::move(path), product});
    }
    return result;
}

std::mutex registry_mutex;
std::vector<Pad> registry;
bool registry_active = false;
}

struct Reader::Worker {
    struct Slot {
        std::wstring path;
        uint16_t product = 0;
        bool bluetooth = false, writable = false;
        HANDLE file = INVALID_HANDLE_VALUE;
        OVERLAPPED io{};
        bool pending = false, failed = false;
        std::vector<uint8_t> buffer;
        size_t feature_length = 0;
        bool connected = false, lizard_off = false;
        ULONGLONG last_state = 0, last_lizard = 0;
        PadReading reading;
        std::optional<Battery> battery;

        Slot() = default;
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
        ~Slot() {
            if (file != INVALID_HANDLE_VALUE) {
                if (pending) { CancelIoEx(file, &io); DWORD done = 0; GetOverlappedResult(file, &io, &done, TRUE); }
                CloseHandle(file);
            }
            if (io.hEvent) CloseHandle(io.hEvent);
        }
        void read() {
            ResetEvent(io.hEvent);
            DWORD done = 0;
            if (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &done, &io) || GetLastError() == ERROR_IO_PENDING)
                pending = true;
            else failed = true;
        }
        bool feature(std::vector<uint8_t> report) {
            return writable && HidD_SetFeature(file, report.data(), static_cast<ULONG>(report.size()));
        }
        bool live(ULONGLONG now) const { return connected && (bluetooth || now - last_state < stale_ms); }
    };

    HWND window;
    HANDLE stop;
    std::vector<std::unique_ptr<Slot>> slots;
    std::thread thread;

    explicit Worker(HWND owner) : window(owner), stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        { std::lock_guard lock(registry_mutex); registry.clear(); registry_active = true; }
        if (stop) thread = std::thread([this] { run(); });
    }
    ~Worker() {
        if (stop) SetEvent(stop);
        if (thread.joinable()) thread.join();
        if (stop) CloseHandle(stop);
        std::lock_guard lock(registry_mutex);
        registry.clear();
        registry_active = false;
    }

    static std::unique_ptr<Slot> open(const Interface& item) {
        auto slot = std::make_unique<Slot>();
        slot->path = item.path;
        slot->product = item.product;
        slot->bluetooth = bluetoothPath(item.path, item.product);
        // Shared access: Steam, SDL games and other tools may open the same collection.
        constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        slot->file = CreateFileW(item.path.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        slot->writable = slot->file != INVALID_HANDLE_VALUE;
        if (!slot->writable)
            slot->file = CreateFileW(item.path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (slot->file == INVALID_HANDLE_VALUE) return nullptr;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (!HidD_GetPreparsedData(slot->file, &preparsed)) return nullptr;
        const bool described = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
        HidD_FreePreparsedData(preparsed);
        if (!described || caps.InputReportByteLength < 18) return nullptr;
        slot->buffer.resize(caps.InputReportByteLength);
        slot->feature_length = caps.FeatureReportByteLength;
        if (slot->feature_length < 6) slot->writable = false;
        slot->io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!slot->io.hEvent) return nullptr;
        slot->read();
        if (slot->failed) return nullptr;
        return slot;
    }

    void scan() {
        const auto found = interfaces();
        std::erase_if(slots, [&](const auto& slot) {
            return std::none_of(found.begin(), found.end(), [&](const Interface& item) { return item.path == slot->path; });
        });
        for (const auto& item : found) {
            if (std::any_of(slots.begin(), slots.end(), [&](const auto& slot) { return slot->path == item.path; })) continue;
            if (auto slot = open(item)) slots.push_back(std::move(slot));
        }
    }

    static void handle(Slot& slot, std::span<const uint8_t> report, ULONGLONG now) {
        if (auto state = parseState(report)) {
            slot.reading = std::move(*state);
            slot.reading.id = slot.path;
            slot.connected = true;
            slot.last_state = now;
        } else if (auto battery = parseBattery(report)) slot.battery = battery;
        else if (const auto link = parseWireless(report); link && !*link) slot.connected = false;
    }

    static void service(Slot& slot, ULONGLONG now) {
        // Drain what completed since the last wait; HID buffers reports between reads.
        for (int i = 0; i < 16 && slot.pending && !slot.failed; ++i) {
            DWORD done = 0;
            if (!GetOverlappedResult(slot.file, &slot.io, &done, FALSE)) {
                if (GetLastError() != ERROR_IO_INCOMPLETE) { slot.pending = false; slot.failed = true; }
                return;
            }
            slot.pending = false;
            handle(slot, {slot.buffer.data(), std::min<size_t>(done, slot.buffer.size())}, now);
            slot.read();
        }
    }

    // Lizard mode off only while the launcher is in front and Steam doesn't own the pad.
    static void heartbeat(Slot& slot, bool own, bool steam, ULONGLONG now) {
        if (!slot.writable) return;
        if (!slot.live(now)) { slot.lizard_off = false; slot.last_lizard = 0; return; } // Reconnects in lizard mode.
        if (own) {
            if (now - slot.last_lizard < heartbeat_ms) return;
            slot.last_lizard = now;
            if (slot.feature(lizardReport(false, slot.feature_length))) slot.lizard_off = true;
        } else if (slot.lizard_off) {
            if (!steam) slot.feature(lizardReport(true, slot.feature_length));
            slot.lizard_off = false;
            slot.last_lizard = 0;
        }
    }

    void publish(bool steam, ULONGLONG now) {
        std::vector<Pad> live;
        for (const auto& slot : slots) {
            if (!slot->live(now)) continue;
            Pad pad{slot->path, slot->product, slot->reading, receiver(slot->product), slot->bluetooth, !steam};
            if (slot->battery) { pad.reading.battery = slot->battery->level; pad.reading.charging = slot->battery->charging; }
            live.push_back(std::move(pad));
        }
        std::lock_guard lock(registry_mutex);
        registry = std::move(live);
    }

    void run() {
        ULONGLONG next_scan = 0, next_steam = 0;
        bool steam = false;
        for (;;) {
            const auto now = GetTickCount64();
            if (now >= next_steam) { steam = steamRunning(); next_steam = now + steam_check_ms; }
            if (now >= next_scan) { scan(); next_scan = now + rescan_ms; }
            std::vector<HANDLE> waits{stop};
            for (const auto& slot : slots)
                if (slot->pending && waits.size() < MAXIMUM_WAIT_OBJECTS) waits.push_back(slot->io.hEvent);
            const auto result = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, 100);
            if (result == WAIT_OBJECT_0) break;
            if (result == WAIT_FAILED) Sleep(100);
            const auto time = GetTickCount64();
            for (auto& slot : slots) service(*slot, time);
            std::erase_if(slots, [](const auto& slot) { return slot->failed; });
            const bool own = !steam && GetForegroundWindow() == window;
            for (auto& slot : slots) heartbeat(*slot, own, steam, time);
            publish(steam, time);
        }
        const bool steam_now = steamRunning();
        for (auto& slot : slots)
            if (slot->lizard_off && !steam_now) slot->feature(lizardReport(true, slot->feature_length));
        slots.clear();
    }
};

Reader::Reader(HWND window) : worker_(std::make_unique<Worker>(window)) {}
Reader::~Reader() = default;

std::vector<Pad> pads() {
    std::lock_guard lock(registry_mutex);
    return registry;
}

bool active() {
    std::lock_guard lock(registry_mutex);
    return registry_active;
}

bool known(const std::wstring& path) {
    std::lock_guard lock(registry_mutex);
    return std::any_of(registry.begin(), registry.end(), [&](const Pad& pad) { return pad.path == path; });
}

void click(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    size_t length = 0;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(file, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) length = caps.OutputReportByteLength;
        HidD_FreePreparsedData(preparsed);
    }
    OVERLAPPED io{};
    io.hEvent = length >= 4 ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
    if (io.hEvent) {
        auto bytes = clickReport(length);
        DWORD sent = 0;
        if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &sent, &io) && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(io.hEvent, 40) != WAIT_OBJECT_0) CancelIoEx(file, &io);
            GetOverlappedResult(file, &io, &sent, TRUE); // Drain before releasing the report/event.
        }
        CloseHandle(io.hEvent);
    }
    CloseHandle(file);
}
}
