#pragma once
#include "json.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <optional>
#include <set>

namespace plugins {
inline void validateQueued(const json::Value& response) {
    const auto* queued = response.get("queued");
    if (!queued) return;
    const auto& count = wire::field(*queued, "count");
    const auto title = text_encoding::wide(wire::text(*queued, "title"));
    if (count.type != json::Value::Type::number || !std::isfinite(count.numeric) || count.numeric < 1 || count.numeric > 1000 ||
        std::floor(count.numeric) != count.numeric || title.empty() || title.size() > 1024) throw std::runtime_error("Invalid queue acknowledgement");
}
struct Download {
    std::string id, command, phase;
    std::wstring title, status, destination;
    std::optional<float> fraction;
    std::wstring artwork_name, platform;
    std::string title_id;
    bool moving() const { return phase == "downloading" || phase == "converting" || phase == "verifying"; }
};
inline std::vector<Download> parseDownloads(const json::Value& response) {
    using namespace wire;
    const auto& items = field(response, "downloads");
    if (items.type != json::Value::Type::array || items.elements.size() > 1000) throw std::runtime_error("Invalid download snapshot");
    std::vector<Download> out;
    std::set<std::string> ids;
    for (const auto& item : items.elements) {
        Download d;
        d.id = text(item, "id"); d.command = text(item, "command"); d.phase = text(item, "phase");
        d.title = text_encoding::wide(text(item, "title")); d.status = text_encoding::wide(text(item, "status"));
        d.destination = text_encoding::wide(text(item, "destination"));
        for (const auto* name : {"artworkName", "platform", "titleId"}) if (const auto* value = item.get(name)) {
            if (value->type != json::Value::Type::string || value->text.size() > 1024) throw std::runtime_error("Invalid download artwork identity");
        }
        d.artwork_name = text_encoding::wide(text(item, "artworkName"));
        d.platform = text_encoding::wide(text(item, "platform")); d.title_id = text(item, "titleId");
        if (field(item, "status").type != json::Value::Type::string || field(item, "destination").type != json::Value::Type::string)
            throw std::runtime_error("Invalid download text");
        const auto command = [](const std::string& s) { return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        }); };
        if (!command(d.id) || !ids.insert(d.id).second || !command(d.command) || d.title.empty() || d.title.size() > 512 ||
            d.status.size() > 1024 || d.destination.size() > 32768 ||
            (d.phase != "queued" && d.phase != "downloading" && d.phase != "converting" && d.phase != "verifying" && d.phase != "paused" && d.phase != "done" && d.phase != "failed" && d.phase != "cancelled"))
            throw std::runtime_error("Invalid download row");
        if (const auto* fraction = item.get("fraction"); fraction && fraction->type != json::Value::Type::null_value) {
            if (fraction->type != json::Value::Type::number || !std::isfinite(fraction->numeric) || fraction->numeric < 0 || fraction->numeric > 1)
                throw std::runtime_error("Invalid download progress");
            d.fraction = static_cast<float>(fraction->numeric);
        }
        out.push_back(std::move(d));
    }
    return out;
}
}
