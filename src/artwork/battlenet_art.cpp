#include "battlenet_art.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>

namespace {
constexpr std::string_view data_root = "https://us.cdn.blizzard.com/tpr/catalogs/";
constexpr uint64_t lifetime_ms = 30ull * 24 * 60 * 60 * 1000;

bool md5(std::string_view text) {
    return text.size() == 32 && std::all_of(text.begin(), text.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}
std::string lower(std::string text) {
    for (char& ch : text) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return text;
}
std::string pathUrl(std::string_view kind, std::string_view hash) {
    return std::string(data_root) + std::string(kind) + "/" + std::string(hash.substr(0, 2)) + "/" + std::string(hash.substr(2, 2)) + "/" + std::string(hash);
}
uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string text(const HttpResponse& reply) {
    return reply.status == 200 ? std::string(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()) : std::string{};
}
}

std::string battlenetBuildConfig(std::string_view versions) {
    int best = -1;
    std::string config;
    size_t start = 0;
    while (start < versions.size()) {
        auto end = versions.find('\n', start);
        if (end == std::string_view::npos) end = versions.size();
        auto line = versions.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.starts_with("PUB-")) continue;
        const auto bar = line.find('|');
        if (bar == std::string_view::npos) continue;
        const auto next = line.find('|', bar + 1);
        const auto hash = line.substr(bar + 1, next == std::string_view::npos ? std::string_view::npos : next - bar - 1);
        const int number = std::atoi(std::string(line.substr(4, bar - 4)).c_str());
        if (md5(hash) && number > best) { best = number; config = std::string(hash); }
    }
    return config;
}

std::string battlenetRoot(std::string_view build_config) {
    const auto at = build_config.find("root = ");
    if (at == std::string_view::npos) return {};
    const auto hash = build_config.substr(at + 7, 32);
    return md5(hash) ? std::string(hash) : std::string{};
}

std::vector<std::pair<std::string, std::string>> battlenetFragments(std::string_view root) {
    std::vector<std::pair<std::string, std::string>> result;
    const auto parsed = json::parse(root);
    const auto* fragments = parsed ? parsed->get("fragments") : nullptr;
    if (!fragments || fragments->type != json::Value::Type::array) return result;
    for (const auto& fragment : fragments->elements) {
        if (fragment.get("requires")) continue; // Regional, CN, old-client or feature variants.
        const auto* name = fragment.get("name");
        const auto* hash = fragment.get("hash");
        if (!name || !hash || name->type != json::Value::Type::string || !md5(hash->stringOr())) continue;
        result.emplace_back(name->text, hash->text);
    }
    return result;
}

std::string battlenetDataUrl(std::string_view hash) { return md5(hash) ? pathUrl("data", hash) : std::string{}; }

std::map<std::string, BattlenetArt> battlenetCatalogArt(const std::vector<std::pair<std::string, std::string>>& fragments) {
    struct Refs { std::string fragment, tall, logo; };
    std::map<std::string, std::vector<Refs>> products;
    std::map<std::string, std::string> files, localized; // Reference → data URL ("default" locale first).
    for (const auto& [name, body] : fragments) {
        const auto parsed = json::parse(body);
        if (!parsed) continue;
        if (const auto* sections = parsed->get("files"); sections && sections->type == json::Value::Type::object)
            for (const auto& [locale, entries] : sections->properties) {
                if (entries.type != json::Value::Type::object) continue;
                for (const auto& [reference, file] : entries.properties) {
                    const auto hash = file.get("hash") ? file.get("hash")->stringOr() : std::string{};
                    const auto path = lower(file.get("name") ? file.get("name")->stringOr() : std::string{});
                    // JPEG/PNG only: the decoder's formats (WebP and SVG are left out).
                    if (!md5(hash) || !(path.ends_with(".jpg") || path.ends_with(".jpeg") || path.ends_with(".png"))) continue;
                    (locale == "default" ? files : localized).emplace(reference, pathUrl("data", hash));
                }
            }
        const auto* list = parsed->get("products");
        if (!list || list->type != json::Value::Type::array) continue;
        for (const auto& product : list->elements) {
            const auto* base = product.get("base");
            const auto* program = base ? base->get("program_id") : nullptr;
            if (!program || program->type != json::Value::Type::string || program->text.empty()) continue;
            products[lower(program->text)].push_back({name, base->get("key_art") ? base->get("key_art")->stringOr() : std::string{},
                                                      base->get("logo") ? base->get("logo")->stringOr() : std::string{}});
        }
    }
    const auto resolve = [&](const std::string& reference) {
        if (const auto found = files.find(reference); found != files.end()) return found->second;
        if (const auto found = localized.find(reference); found != localized.end()) return found->second;
        return std::string{};
    };
    std::map<std::string, BattlenetArt> result;
    for (const auto& [program, claims] : products) {
        const Refs* chosen = claims.size() == 1 ? &claims.front() : nullptr;
        if (!chosen) for (const auto& claim : claims) if (lower(claim.fragment) == program) chosen = &claim;
        if (!chosen) continue; // Claimed by several games: none of them is this one for sure.
        BattlenetArt art{resolve(chosen->tall), resolve(chosen->logo)};
        if (!art.tall.empty() || !art.logo.empty()) result[program] = std::move(art);
    }
    if (const auto drtl = result.find("drtl"); drtl != result.end() && !result.contains("d1")) result["d1"] = drtl->second;
    return result;
}

std::string battlenetArtUrl(const std::string& code, bool logo, const std::filesystem::path& cache, const ArtworkHttp& get) {
    static std::mutex mutex;
    static std::map<std::string, BattlenetArt> known;
    static uint64_t loaded_at = 0, failed_at = 0;
    std::lock_guard lock(mutex); // One catalog walk at a time; the other workers wait for its answer.
    const auto file = cache.parent_path() / L"artwork" / L"battlenet-art.v1";
    const auto now = unixMs();
    if (!loaded_at) {
        std::ifstream saved(file);
        uint64_t at = 0;
        if (saved >> at && now - at < lifetime_ms) {
            std::string line;
            std::getline(saved, line);
            while (std::getline(saved, line)) {
                const auto first = line.find('\t'), second = line.find('\t', first == std::string::npos ? 0 : first + 1);
                if (first == std::string::npos || second == std::string::npos) continue;
                known[line.substr(0, first)] = {line.substr(first + 1, second - first - 1), line.substr(second + 1)};
            }
            loaded_at = at;
        }
    }
    if (!loaded_at && (!failed_at || now - failed_at > 60ull * 60 * 1000)) {
        const auto ask = [&](const std::string& url) { return text(get(std::wstring(url.begin(), url.end()), L"Accept: */*\r\n", 4 * 1024 * 1024)); };
        const auto config = battlenetBuildConfig(ask("https://us.version.battle.net/v2/products/catalogs/versions"));
        const auto root = config.empty() ? std::string{} : battlenetRoot(ask(pathUrl("config", config)));
        std::vector<std::pair<std::string, std::string>> fragments;
        if (!root.empty()) for (const auto& [name, hash] : battlenetFragments(ask(battlenetDataUrl(root)))) {
            auto body = ask(battlenetDataUrl(hash));
            if (!body.empty()) fragments.emplace_back(name, std::move(body));
        }
        auto art = battlenetCatalogArt(fragments);
        if (art.empty()) failed_at = now;
        else {
            known = std::move(art);
            loaded_at = now;
            std::ostringstream out;
            out << now << '\n';
            for (const auto& [program, entry] : known) out << program << '\t' << entry.tall << '\t' << entry.logo << '\n';
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            atomic_file::write(file, out.str());
        }
    }
    const auto found = known.find(lower(code));
    if (found == known.end()) return {};
    return logo ? found->second.logo : found->second.tall;
}
