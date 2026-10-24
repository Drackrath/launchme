#include "../src/artwork/text_crop.hpp"
#include "../src/library/steam.hpp"
#include "../src/library/shortcuts.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/net/json.hpp"
#include "../src/model/key_codec.hpp"
#include "../src/platform/text_encoding.hpp"
#include <Windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
std::string read(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || static_cast<size_t>(input.tellg()) > limit) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0'); input.seekg(0);
    input.read(text.data(), static_cast<std::streamsize>(text.size())); return text;
}
using text_encoding::wide;
std::string decodedKey(const std::string& hex) { return key_codec::decode(hex).value_or(std::string{}); }
struct Job { std::filesystem::path image; std::string key; std::wstring title; };
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || std::wstring_view(argv[1]) != L"--rebuild") {
        std::cout << "rebuild_crops --rebuild [Steam AppID]\nRebuilds crop metadata only; originals, selections and credentials are never rewritten.\n";
        return 1;
    }
    const std::wstring only = argc > 2 ? argv[2] : L"";
    const auto root = userConfigDirectory();
    std::unordered_map<std::string, std::wstring> names;
    const auto add = [&](const Game& game) { if (!game.name.empty()) names[gameKey(game)] = game.name; };
    const auto steam = findSteamRoot();
    for (const auto& game : scanInstalledGames(steam)) add(game);
    for (const auto& shortcut : scanSteamShortcuts(steam)) {
        add(shortcut.game); names["steam:" + std::to_string(shortcut.game.app_id)] = shortcut.game.name;
    }
    const auto pc = scanPcLibraries(root / L"libraries");
    for (const auto& game : pc.visible) add(game);
    for (const auto& game : pc.private_games) add(game);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root / L"metadata", ec)) {
        const auto file = entry.path();
        const auto id = file.filename().string().substr(0, file.filename().string().find('.'));
        if (id.empty() || id.find_first_not_of("0123456789") != id.npos) continue;
        if (file.extension() == L".txt") {
            const auto title = wide(read(file, 4096));
            if (!title.empty()) names["steam:" + id] = title;
        } else if (file.filename().wstring().ends_with(L".details.json")) {
            const auto json = json::parse(read(file, 4 * 1024 * 1024));
            const auto item = json ? json->get(id) : nullptr;
            const auto data = item ? item->get("data") : nullptr;
            const auto name = data ? data->get("name") : nullptr;
            if (name) names["steam:" + id] = wide(name->stringOr());
        }
    }
    std::vector<Job> jobs;
    const auto append = [&](const std::filesystem::path& file, const std::string& key) {
        if (!only.empty() && wide(key) != L"steam:" + only) return;
        const auto found = names.find(key);
        jobs.push_back({file, key, found == names.end() ? L"" : found->second});
    };
    for (const auto& directory : {root / L"covers", root / L"emulation" / L"covers"}) {
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != L".img") continue;
            const auto filename = entry.path().filename().string();
            const auto id = filename.substr(0, filename.find('.'));
            if (!id.empty() && id.find_first_not_of("0123456789") == id.npos) append(entry.path(), "steam:" + id);
        }
        ec.clear();
    }
    for (const auto& directory : {root / L"stores", root / L"artwork" / L"selections"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
            const auto file = entry.path();
            if (!entry.is_regular_file() || file.extension() != L".img") continue;
            if (file.parent_path().filename() == L"cover") append(file, decodedKey(file.parent_path().parent_path().filename().string()));
            else if (file.filename().wstring().starts_with(L"cover.")) append(file, decodedKey(file.parent_path().filename().string()));
        }
        ec.clear();
    }
    size_t unknown = 0;
    for (const auto& job : jobs) if (job.title.empty()) ++unknown;
    std::cout << "Original covers=" << jobs.size() << "; without locally known title=" << unknown << "; cache v" << crop_cache_version << '\n';
    std::atomic_size_t next{0}, done{0}, failed{0};
    std::mutex output;
    const auto worker = [&] {
        while (true) {
            const auto index = next.fetch_add(1);
            if (index >= jobs.size()) return;
            const auto& job = jobs[index];
            try {
                auto metadata = job.image; metadata.replace_extension(L".focus");
                const auto focus = cachedTextCrop(job.image, metadata, job.title, true);
                int version = 0; std::ifstream(metadata) >> version;
                if (version != crop_cache_version && !manualTextCrop(job.image)) ++failed;
                if (!only.empty()) { std::lock_guard lock(output); std::cout << "Focus=" << focus.x << ',' << focus.y << "; stored v" << version << '\n'; }
            } catch (...) { ++failed; }
            const auto count = done.fetch_add(1) + 1;
            if (count % 50 == 0) { std::lock_guard lock(output); std::cout << "Processed " << count << '/' << jobs.size() << std::endl; }
        }
    };
    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) workers.emplace_back(worker);
    for (auto& thread : workers) thread.join();
    std::cout << "Rebuilt=" << done - failed << "; failed=" << failed << "; originals unchanged\n";
    return failed ? 2 : 0;
}
