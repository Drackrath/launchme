#include "steam_controller.hpp"
#include "linux_hid.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <utility>

// /dev/hidraw* and poll(); the parsers are in steam_controller.cpp. SDL 3.4 has no driver for
// this controller yet (only the first generation and the Deck), so it is read here as on Windows.
namespace steam_controller {
namespace {
constexpr ULONGLONG rescan_ms = 2000, heartbeat_ms = 2000, steam_check_ms = 2000;
// USB and the Puck stream state at ~64 Hz even when idle; a silent slot is empty or asleep.
constexpr ULONGLONG stale_ms = 1500;
constexpr size_t report_length = 64; // Input, output and feature reports, with the report ID.

bool receiver(uint16_t product) { return product == 0x1304 || product == 0x1305; }
// The client's process is "steam" (native, Flatpak and Snap alike); other users' don't hold our pads.
bool steamRunning() { return linux_hid::processRunning("steam"); }

struct Interface { std::wstring path; uint16_t product = 0; bool bluetooth = false; };
// Each hidraw node is one HID interface; the Puck has one per slot.
std::vector<Interface> interfaces() {
    std::vector<Interface> result;
    for (const auto& node : linux_hid::hidraws()) {
        if (!supported(node.hid.vendor, node.hid.product)) continue;
        const auto collections = linux_hid::topCollections(node.descriptor);
        if (std::none_of(collections.begin(), collections.end(), [](const auto& c) { return inputCollection(c.first, c.second); })) continue;
        result.push_back({node.node.wstring(), node.hid.product, node.hid.product == 0x1303 || node.hid.bus == linux_hid::bus_bluetooth});
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
        bool bluetooth = false, writable = false, failed = false;
        int file = -1;
        bool connected = false, lizard_off = false;
        ULONGLONG last_state = 0, last_lizard = 0;
        PadReading reading;
        std::optional<Battery> battery;

        Slot() = default;
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
        ~Slot() { if (file >= 0) close(file); }
        bool feature(std::vector<uint8_t> report) {
            return writable && ioctl(file, HIDIOCSFEATURE(report.size()), report.data()) >= 0;
        }
        bool live(ULONGLONG now) const { return connected && (bluetooth || now - last_state < stale_ms); }
    };

    void* window;
    int stop;
    std::vector<std::unique_ptr<Slot>> slots;
    std::thread thread;

    explicit Worker(void* owner) : window(owner), stop(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
        { std::lock_guard lock(registry_mutex); registry.clear(); registry_active = true; }
        if (stop >= 0) thread = std::thread([this] { run(); });
    }
    ~Worker() {
        if (stop >= 0) { const uint64_t one = 1; [[maybe_unused]] const auto written = write(stop, &one, sizeof(one)); }
        if (thread.joinable()) thread.join();
        if (stop >= 0) close(stop);
        std::lock_guard lock(registry_mutex);
        registry.clear();
        registry_active = false;
    }

    // hidraw allows any number of readers, so Steam and SDL games can open the pad as well.
    static std::unique_ptr<Slot> open(const Interface& item) {
        auto slot = std::make_unique<Slot>();
        slot->path = item.path;
        slot->product = item.product;
        slot->bluetooth = item.bluetooth;
        const auto path = std::filesystem::path(item.path);
        slot->file = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        slot->writable = slot->file >= 0;
        if (!slot->writable) slot->file = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (slot->file < 0) return nullptr;
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

    // Drain what arrived since the last wait; hidraw buffers reports between reads.
    static void service(Slot& slot, ULONGLONG now) {
        std::array<uint8_t, 256> buffer{};
        for (int i = 0; i < 16; ++i) {
            const auto done = ::read(slot.file, buffer.data(), buffer.size());
            if (done < 0) { if (errno != EAGAIN && errno != EINTR) slot.failed = true; return; }
            if (done == 0) return;
            handle(slot, {buffer.data(), static_cast<size_t>(done)}, now);
        }
    }

    // Lizard mode off only while the launcher is in front and Steam doesn't own the pad.
    static void heartbeat(Slot& slot, bool own, bool steam, ULONGLONG now) {
        if (!slot.writable) return;
        if (!slot.live(now)) { slot.lizard_off = false; slot.last_lizard = 0; return; } // Reconnects in lizard mode.
        if (own) {
            if (now - slot.last_lizard < heartbeat_ms) return;
            slot.last_lizard = now;
            if (slot.feature(lizardReport(false, report_length))) slot.lizard_off = true;
        } else if (slot.lizard_off) {
            if (!steam) slot.feature(lizardReport(true, report_length));
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

    bool foreground() const {
        return !window || (SDL_GetWindowFlags(static_cast<SDL_Window*>(window)) & SDL_WINDOW_INPUT_FOCUS) != 0;
    }

    void run() {
        ULONGLONG next_scan = 0, next_steam = 0;
        bool steam = false;
        for (;;) {
            const auto now = GetTickCount64();
            if (now >= next_steam) { steam = steamRunning(); next_steam = now + steam_check_ms; }
            if (now >= next_scan) { scan(); next_scan = now + rescan_ms; }
            std::vector<pollfd> waits{{stop, POLLIN, 0}};
            for (const auto& slot : slots) waits.push_back({slot->file, POLLIN, 0});
            if (poll(waits.data(), waits.size(), 100) < 0 && errno != EINTR) Sleep(100);
            if (waits[0].revents) break;
            const auto time = GetTickCount64();
            for (size_t i = 0; i < slots.size(); ++i) {
                if (waits[i + 1].revents & (POLLERR | POLLHUP | POLLNVAL)) slots[i]->failed = true; // Unplugged.
                else if (waits[i + 1].revents & POLLIN) service(*slots[i], time);
            }
            std::erase_if(slots, [](const auto& slot) { return slot->failed; });
            const bool own = !steam && foreground();
            for (auto& slot : slots) heartbeat(*slot, own, steam, time);
            publish(steam, time);
        }
        const bool steam_now = steamRunning();
        for (auto& slot : slots)
            if (slot->lizard_off && !steam_now) slot->feature(lizardReport(true, report_length));
        slots.clear();
    }
};

Reader::Reader(void* window) : worker_(std::make_unique<Worker>(window)) {}
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
    const int file = ::open(std::filesystem::path(path).c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (file < 0) return;
    const auto bytes = clickReport(report_length);
    // Non-blocking: a pad that doesn't take the report within 40 ms is skipped, as on Windows.
    pollfd ready{file, POLLOUT, 0};
    if (poll(&ready, 1, 40) > 0 && (ready.revents & POLLOUT)) { [[maybe_unused]] const auto sent = write(file, bytes.data(), bytes.size()); }
    close(file);
}
}
