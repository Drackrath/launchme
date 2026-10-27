#include "gog_art.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

namespace {
constexpr uint64_t found_lifetime_ms = 30ull * 24 * 60 * 60 * 1000, missing_lifetime_ms = 7ull * 24 * 60 * 60 * 1000;
uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
bool productId(std::string_view id) {
    return !id.empty() && id.size() <= 12 && std::all_of(id.begin(), id.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}
}

GogArt parseGogArt(std::string_view v2_games) {
    GogArt art;
    const auto parsed = json::parse(v2_games);
    const auto* links = parsed ? parsed->get("_links") : nullptr;
    if (!links) return art;
    const auto link = [&](const char* name) -> std::string {
        const auto* entry = links->get(name);
        auto href = entry && entry->get("href") ? entry->get("href")->stringOr() : std::string{};
        if (href.starts_with("//")) href = "https:" + href;
        std::string lower = href;
        for (char& ch : lower) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        const bool image = lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".png");
        return href.starts_with("https://") && image && href.size() <= 1024 && href.find_first_of("\t\r\n") == std::string::npos ? href : std::string{};
    };
    art.tall = link("boxArtImage");
    art.wide = link("galaxyBackgroundImage");
    if (art.wide.empty()) art.wide = link("backgroundImage");
    art.logo = link("logo");
    return art;
}

GogArt gogArt(const std::string& product_id, const std::filesystem::path& cache, const ArtworkHttp& get) {
    if (!productId(product_id)) return {};
    const auto file = cache.parent_path() / L"artwork" / L"gog-art" / (std::wstring(product_id.begin(), product_id.end()) + L".v1");
    const auto now = unixMs();
    {
        std::ifstream saved(file);
        std::string line;
        if (std::getline(saved, line)) {
            GogArt art;
            uint64_t at = 0;
            size_t start = 0;
            std::vector<std::string> parts;
            for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) parts.push_back(line.substr(start, tab - start));
            parts.push_back(line.substr(start));
            if (parts.size() == 4) {
                at = std::strtoull(parts[0].c_str(), nullptr, 10);
                art = {parts[1], parts[2], parts[3]};
                const bool any = !art.tall.empty() || !art.wide.empty() || !art.logo.empty();
                if (now - at < (any ? found_lifetime_ms : missing_lifetime_ms)) return art;
            }
        }
    }
    const auto reply = get(L"https://api.gog.com/v2/games/" + std::wstring(product_id.begin(), product_id.end()) + L"?locale=en-US",
                           L"Accept: application/json\r\n", 1024 * 1024);
    if (reply.status != 200 && reply.status != 404) return {}; // Unanswered: asked again next time.
    const auto art = reply.status == 200 ? parseGogArt(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size())) : GogArt{};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    atomic_file::write(file, std::to_string(now) + '\t' + art.tall + '\t' + art.wide + '\t' + art.logo + '\n');
    return art;
}
