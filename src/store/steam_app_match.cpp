#include "steam_app_match.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <chrono>
#include <fstream>

namespace {
uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
constexpr uint64_t miss_lifetime_ms = 7ull * 24 * 60 * 60 * 1000;
}

uint32_t matchSteamApp(std::wstring_view title, std::string_view storesearch) {
    const auto parsed = json::parse(storesearch);
    const auto* items = parsed ? parsed->get("items") : nullptr;
    if (!items || items->type != json::Value::Type::array) return 0;
    std::vector<GridGame> apps;
    for (const auto& item : items->elements) {
        const auto* type = item.get("type");
        const auto* id = item.get("id");
        const auto* name = item.get("name");
        if (!type || type->stringOr() != "app" || !id || id->type != json::Value::Type::number || id->numeric < 1 ||
            id->numeric > 4294967295.0 || !name || name->type != json::Value::Type::string) continue;
        apps.push_back({{static_cast<uint32_t>(id->numeric), true}, text_encoding::wide(name->text), 0});
    }
    return matchGridGame(title, apps).id;
}

uint32_t steamAppForTitle(const std::wstring& title, const std::filesystem::path& metadata, const std::string& cache_key,
                          const ArtworkHttp& get) {
    if (title.empty() || cache_key.empty()) return 0;
    const auto file = metadata / (text_encoding::wide(cache_key) + L".steamapp");
    {
        std::ifstream saved(file);
        uint64_t app = 0, at = 0;
        if (saved >> app >> at && app <= UINT32_MAX && (app || unixMs() - at < miss_lifetime_ms)) return static_cast<uint32_t>(app);
    }
    const auto reply = get(L"https://store.steampowered.com/api/storesearch/?term=" + url_encoding::component(std::wstring_view(title)) +
                           L"&l=english&cc=US", L"Accept: application/json\r\n", 512 * 1024);
    if (reply.status != 200) return 0; // No answer is no "no match": asked again next time.
    const auto app = matchSteamApp(title, std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
    std::error_code ec;
    std::filesystem::create_directories(metadata, ec);
    atomic_file::write(file, std::to_string(app) + " " + std::to_string(unixMs()));
    return app;
}
