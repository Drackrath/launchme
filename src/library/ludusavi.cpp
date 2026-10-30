#include "ludusavi.hpp"
#include "../platform/file_io.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/helper_process.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#ifndef _WIN32
#include "xdg_paths.hpp"
#include <sys/stat.h>
#endif

#include <algorithm>

namespace ludusavi {
namespace {
#ifdef _WIN32
constexpr const wchar_t* program_name = L"ludusavi.exe";
constexpr std::string_view asset_suffix = "-win64.zip";
#else
constexpr const wchar_t* program_name = L"ludusavi";
constexpr std::string_view asset_suffix = "-linux.tar.gz";
#endif

bool isFile(const std::filesystem::path& path) { return file_io::isFile(path); }

// PATH's ludusavi (Windows: also with its .exe).
std::filesystem::path onPath() {
    const auto search = system_info::environment(L"PATH", 32767);
    if (!search) return {};
#ifdef _WIN32
    constexpr wchar_t separator = L';';
#else
    constexpr wchar_t separator = L':';
#endif
    std::wstring_view rest(*search);
    while (!rest.empty()) {
        const auto end = std::min(rest.find(separator), rest.size());
        if (const auto folder = rest.substr(0, end); !folder.empty())
            if (const auto candidate = std::filesystem::path(folder) / program_name; candidate.is_absolute() && isFile(candidate)) return candidate;
        rest.remove_prefix(std::min(end + 1, rest.size()));
    }
    return {};
}
} // namespace

std::filesystem::path ownFolder(const std::filesystem::path& data) { return data / L"tools" / L"ludusavi"; }

std::filesystem::path find(const std::filesystem::path& data) {
    if (const auto own = ownFolder(data) / program_name; isFile(own)) return own;
    if (auto path = onPath(); !path.empty()) return path;
#ifndef _WIN32
    if (auto flatpak = xdg_paths::flatpakWrapper(L"com.github.mtkennerly.ludusavi"); !flatpak.empty()) return flatpak;
#endif
    return {};
}

std::optional<Release> parseLatest(std::string_view json) {
    const auto root = json::parse(json, 4 * 1024 * 1024);
    if (!root) return std::nullopt;
    Release release;
    if (const auto* tag = root->get("tag_name")) release.tag = tag->stringOr();
    const auto* assets = root->get("assets");
    if (!assets || assets->type != json::Value::Type::array) return std::nullopt;
    for (const auto& asset : assets->elements) {
        const auto name = asset.get("name") ? asset.get("name")->stringOr() : std::string{};
        const auto url = asset.get("browser_download_url") ? asset.get("browser_download_url")->stringOr() : std::string{};
        if (!name.ends_with(asset_suffix) || name.find("legacy") != std::string::npos || !url.starts_with("https://github.com/mtkennerly/ludusavi/")) continue;
        release.asset = name;
        release.url = text_encoding::wideLenient(url);
        return release;
    }
    return std::nullopt;
}

std::wstring install(const std::filesystem::path& data) {
    const auto latest = httpGet(L"https://api.github.com/repos/mtkennerly/ludusavi/releases/latest", L"", 2 * 1024 * 1024);
    if (latest.status != 200) return L"GitHub: " + std::to_wstring(latest.status);
    const auto release = parseLatest(std::string_view(reinterpret_cast<const char*>(latest.body.data()), latest.body.size()));
    if (!release) return L"No Ludusavi package for this system";
    const auto package = httpGet(release->url, L"", 128 * 1024 * 1024);
    if (package.status != 200 || package.body.size() < 1024) return L"Download failed: " + std::to_wstring(package.status);
    // Unpacked beside the tool's folder first: a failed or partial unpack never leaves a program
    // that find() would pick up (and that would hide the download button).
    const auto folder = ownFolder(data);
    auto staging = folder; staging += L".part";
    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    std::filesystem::create_directories(staging, ec);
    const auto archive = staging / text_encoding::wideLenient(release->asset);
    if (!atomic_file::write(archive, std::span<const uint8_t>(package.body))) return L"Cannot write " + archive.wstring();
    // tar unpacks both: Windows 10's bsdtar reads the zip, Linux's the tar.gz.
#ifdef _WIN32
    const auto system = system_info::environment(L"SystemRoot", 260).value_or(L"C:\\Windows");
    const std::filesystem::path tar = std::filesystem::path(system) / L"System32" / L"tar.exe";
#else
    const std::filesystem::path tar = xdg_paths::findProgram({L"tar"});
#endif
    const auto result = helper_process::run(tar, staging, {L"-xf", archive.wstring(), L"-C", staging.wstring()}, {}, [] { return false; });
    std::filesystem::remove(archive, ec);
    const auto program = staging / program_name;
    if (!result.ok || !isFile(program)) { std::filesystem::remove_all(staging, ec); return L"Cannot unpack " + text_encoding::wideLenient(release->asset); }
#ifndef _WIN32
    ::chmod(program.c_str(), 0755);
#endif
    std::filesystem::remove_all(folder, ec);
    std::filesystem::rename(staging, folder, ec);
    if (ec) { std::filesystem::remove_all(staging, ec); return L"Cannot replace " + folder.wstring(); }
    return {};
}

std::vector<std::wstring> findArguments(const Query& query) {
    std::vector<std::wstring> arguments{L"find", L"--api"};
    if (query.steam_id) arguments.insert(arguments.end(), {L"--steam-id", std::to_wstring(query.steam_id)});
    else if (!query.gog_id.empty()) arguments.insert(arguments.end(), {L"--gog-id", text_encoding::wideLenient(query.gog_id)});
    else arguments.insert(arguments.end(), {L"--normalized", L"--", query.title}); // "--": a title may start with "-".
    return arguments;
}

std::optional<std::wstring> parseFind(std::string_view json) {
    const auto root = json::parse(json, 4 * 1024 * 1024);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::object || games->properties.empty()) return std::nullopt;
    // The best score (one result for an exact ID).
    const std::string* best = nullptr;
    double best_score = -1;
    for (const auto& [title, entry] : games->properties) {
        const auto* score = entry.get("score");
        const double value = score && score->type == json::Value::Type::number ? score->numeric : 1.0;
        if (value > best_score || (value == best_score && best && title < *best)) { best = &title; best_score = value; }
    }
    return best ? std::optional<std::wstring>(text_encoding::wideLenient(*best)) : std::nullopt;
}

std::vector<std::wstring> operationArguments(bool restore, const std::wstring& title, const std::filesystem::path& wine_prefix) {
    std::vector<std::wstring> arguments{restore ? L"restore" : L"backup", L"--api", L"--force"};
    if (!restore && !wine_prefix.empty()) arguments.insert(arguments.end(), {L"--wine-prefix", wine_prefix.wstring()});
    arguments.insert(arguments.end(), {L"--", title}); // A title is never read as an option.
    return arguments;
}

Outcome parseOperation(std::string_view json) {
    Outcome result;
    const auto root = json::parse(json, 8 * 1024 * 1024);
    const auto* overall = root ? root->get("overall") : nullptr;
    if (!overall) return result;
    const auto number = [&](const char* name) {
        const auto* value = overall->get(name);
        return value && value->type == json::Value::Type::number && value->numeric >= 0 ? value->numeric : 0.0;
    };
    // Clamped before the cast: a value past the type's range would be undefined behaviour.
    result.games = static_cast<uint32_t>(std::min(number("processedGames"), 4294967295.0));
    result.bytes = static_cast<uint64_t>(std::min(number("processedBytes"), 1.8e19));
    if (const auto* games = root->get("games"); games && games->type == json::Value::Type::object)
        for (const auto& [title, game] : games->properties)
            if (const auto* files = game.get("files"); files && files->type == json::Value::Type::object) result.files += static_cast<uint32_t>(files->properties.size());
    result.ok = !root->get("errors");
    return result;
}
}
