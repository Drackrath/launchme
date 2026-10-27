#pragma once
#include "../model/controller_models.hpp"
#include "../net/json.hpp"
#include "text_encoding.hpp"
#include "unicode_text.hpp"
#include <filesystem>
#include <fstream>

// Some controllers impersonate another model in Switch/XInput mode. A local
// JSON object maps a full enumerated device ID to a known product name. Match
// only that device, never every pad sharing its spoofed VID/PID or generic name.
inline void applyControllerOverrides(std::vector<devices::Controller>& controllers, std::string_view text) {
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    const auto root = json::parse(text);
    if (!root || root->type != json::Value::Type::object) return;
    for (const auto& [key, value] : root->properties) {
        const auto id = text_encoding::wide(key), name = text_encoding::wide(value.stringOr());
        if (id.empty() || name.empty()) continue;
        const auto picture = devices::classifyController(0, 0, name);
        if (picture == devices::Picture::generic) continue; // Unknown overrides retain automatic detection.
        for (auto& pad : controllers) {
            if (!unicode_text::equalIgnoreCase(pad.id, id)) continue;
            pad.name = name;
            pad.picture = picture;
        }
    }
}

inline void loadControllerOverrides(std::vector<devices::Controller>& controllers, const std::filesystem::path& file) {
    if (controllers.empty()) return;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 64 * 1024) return;
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (input.read(text.data(), static_cast<std::streamsize>(text.size()))) applyControllerOverrides(controllers, text);
}
