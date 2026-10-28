#include "launcher.hpp"
#include "../model/headphones.hpp"
#ifdef _WIN32
#include <winrt/base.h>
#endif

void Launcher::initializeHeadphoneMonitor() {
    requestHeadphonePicture(); // Warm the cache before a Bluetooth connection arrives.
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    work_.start([alive, hwnd, read = read_headphones_, wake = headphones_rescan_] {
#ifdef _WIN32
        winrt::init_apartment();
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
#endif
        std::optional<headphones::State> previous;
        while (alive->load()) {
            wake->take();
            const auto state = read();
            // A failed census is not a disconnection; do not re-toast after a transient error.
            if (state && state != previous) {
                auto message = std::make_unique<HeadphonesMessage>();
                message->connected = state->connected;
                message->battery = state->connected ? state->battery : std::nullopt;
                if (!alive->load() || !PostMessageW(hwnd, headphones_message, 0, reinterpret_cast<LPARAM>(message.get()))) return;
                message.release(); previous = state;
            }
            // A headset arriving or leaving is a device change (WM_DEVICECHANGE wakes this); while one
            // is connected its battery is read every 30 s, otherwise once a minute as a fallback.
#ifdef _WIN32
            WaitForSingleObject(wake->event, previous && previous->connected ? 30000 : 60000);
#else
            ChangeWatch sleep; // No sources: only the wake (an eventfd) ends it early.
            sleep.wake(wake->event);
            sleep.wait(previous && previous->connected ? 30000 : 60000);
#endif
        }
    });
}

void Launcher::requestHeadphonePicture() {
    if (!runtime_started_ || headphone_image_ready_ || headphone_image_requested_) return;
    headphone_image_requested_ = true;
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    if (!work_.start([alive, hwnd] {
#ifdef _WIN32
        winrt::init_apartment();
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
#endif
        auto message = std::make_unique<HeadphonesMessage>();
        message->image = true;
        message->bytes = headphonePicture(userConfigDirectory() / L"headphones");
        if (!alive->load()) return;
        if (PostMessageW(hwnd, headphones_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) headphone_image_requested_ = false;
}

void Launcher::onHeadphones(std::unique_ptr<HeadphonesMessage> message) {
    if (message->image) {
        headphone_image_requested_ = false;
        if (!message->bytes.empty()) {
            headphone_image_ready_ = true;
            renderer_.setPlatformBadge(headphones::badge, std::move(message->bytes));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (message->connected && !headphones_connected_) {
        requestHeadphonePicture();
        showNotification(NotificationKind::headphones, tr(L"Teraz podłączone"), headphones::name);
    }
    // Battery-only updates enrich the existing toast, never replay its sound or restart its age.
    for (auto& notice : notifications_) if (notice.kind == NotificationKind::headphones) {
        notice.connected = message->connected;
        notice.battery = message->connected ? message->battery : std::nullopt;
    }
    headphones_connected_ = message->connected;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
