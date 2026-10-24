#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <memory>
#include <vector>
#include "devices.hpp"

// error: a failed action (red badge, longer on screen, room for a full path or reason).
enum class NotificationKind { controller, volume, launch, headphones, error, download };

struct Notification {
    NotificationKind kind = NotificationKind::controller;
    std::wstring title;
    std::wstring subtitle;
    float level = 0;
    float age = 0;
    bool connected = true;
    devices::Picture controller_picture = devices::Picture::generic;
    std::optional<int> battery; // Unknown is distinct from a known 0%.
    std::wstring device_id; // Keep late battery readings tied to the right controller.
    std::shared_ptr<const std::vector<uint8_t>> cover; // Cached cover, cropped square in the toast.
    std::string art_key;
};
struct VolumeNotice { float level = 0; bool muted = false, input = false; float age = 0; };

inline float notificationEase(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Seconds a toast stays (fade-out included); errors stay long enough to read a path.
inline float notificationLifetime(NotificationKind kind) {
    return kind == NotificationKind::error ? 6.0f : kind == NotificationKind::download ? 4.5f : 2.7f;
}

inline float notificationOpacity(float age, float lifetime = 2.7f) {
    return notificationEase(age / 0.23f) * notificationEase((lifetime - age) / 0.42f);
}
