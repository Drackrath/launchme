#include "companion_photos.hpp"
#include "companion_characters.hpp"
#include "json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <random>

namespace companion::photos {
namespace {
constexpr size_t image_limit = 16 * 1024 * 1024;
std::string readText(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<uint64_t>(input.tellg()) > limit) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
bool pictureName(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".gif" || extension == L".bmp" || extension == L".webp";
}
json::Value strings(const std::vector<std::wstring>& list) {
    auto array = json::array();
    for (const auto& item : list) array.elements.push_back(json::string(text_encoding::utf8Lenient(item)));
    return array;
}
std::vector<std::wstring> strings(const json::Value* value, size_t limit) {
    std::vector<std::wstring> result;
    if (!value || value->type != json::Value::Type::array) return result;
    for (const auto& item : value->elements)
        if (item.type == json::Value::Type::string && !item.text.empty() && result.size() < limit) result.push_back(text_encoding::wideLenient(item.text));
    return result;
}
// A random one of `files`, avoiding the recent ones (names relative to `root`) while others are left.
std::optional<std::filesystem::path> choose(std::vector<std::filesystem::path> files, const std::filesystem::path& root, const std::vector<std::wstring>& recent,
                                            uint32_t seed) {
    if (files.empty()) return std::nullopt;
    std::sort(files.begin(), files.end());
    std::mt19937 random(seed);
    std::shuffle(files.begin(), files.end(), random);
    std::error_code ec;
    for (const auto& file : files) {
        const auto name = std::filesystem::relative(file, root, ec).wstring();
        if (std::find(recent.begin(), recent.end(), name) == recent.end()) return file;
    }
    return files.front(); // All sent lately: the oldest of them comes round again.
}
}

std::filesystem::path folderOf(const std::filesystem::path& config) { return config / L"companion" / L"photos"; }

State load(const std::filesystem::path& folder) {
    State state;
    const auto root = json::parse(readText(folder / L"state.json", 1024 * 1024));
    if (!root || root->type != json::Value::Type::object) return state;
    if (const auto* value = root->get("last_shown")) state.last_shown = value->intOr();
    state.recent = strings(root->get("recent"), 30);
    return state;
}

bool save(const std::filesystem::path& folder, const State& state) {
    auto root = json::object();
    root.properties["version"] = json::number(1);
    root.properties["last_shown"] = json::number(state.last_shown);
    root.properties["recent"] = strings(state.recent);
    return atomic_file::write(folder / L"state.json", json::serialize(root));
}

int turn(uint32_t seed) {
    static constexpr std::array<int, 3> turns{2, 3, 5};
    return turns[seed % turns.size()];
}

bool due(const State& state, int today) { return state.last_shown != today; }

bool imageBytes(std::span<const uint8_t> bytes) {
    const auto starts = [&](std::initializer_list<uint8_t> magic, size_t at = 0) {
        return bytes.size() >= at + magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at));
    };
    return starts({0x89, 'P', 'N', 'G'}) || starts({0xFF, 0xD8, 0xFF}) || starts({'G', 'I', 'F', '8'}) || starts({'B', 'M'}) ||
           (starts({'R', 'I', 'F', 'F'}) && starts({'W', 'E', 'B', 'P'}, 8));
}

int rangeStart(std::wstring_view name) {
    const auto dash = name.find(L'-');
    if (dash == 0 || dash == std::wstring_view::npos || dash + 1 >= name.size() || name.size() > 9) return -1;
    int from = 0, to = 0;
    for (size_t i = 0; i < name.size(); ++i) {
        if (i == dash) continue;
        if (name[i] < L'0' || name[i] > L'9') return -1;
        (i < dash ? from : to) = (i < dash ? from : to) * 10 + (name[i] - L'0');
    }
    return from <= to ? from : -1;
}

std::optional<std::filesystem::path> pick(const std::filesystem::path& folder, Look look, int level, const std::vector<std::wstring>& recent, uint32_t seed) {
    std::error_code ec;
    if (folder.empty()) return std::nullopt;
    const auto key = lookKey(look);
    if (key.empty()) return std::nullopt;
    const auto own = folder / text_encoding::wideLenient(key);
    if (!std::filesystem::is_directory(own, ec)) return std::nullopt;
    std::vector<std::filesystem::path> files;
    const auto take = [&](const std::filesystem::directory_entry& entry) {
        std::error_code size_ec;
        if (files.size() < 5000 && entry.is_regular_file(size_ec) && pictureName(entry.path()) && entry.file_size(size_ec) <= image_limit && !size_ec)
            files.push_back(entry.path());
    };
    for (const auto& entry : std::filesystem::directory_iterator(own, std::filesystem::directory_options::skip_permission_denied, ec)) {
        std::error_code type_ec;
        if (!entry.is_directory(type_ec)) { take(entry); continue; }
        const int from = rangeStart(entry.path().filename().wstring());
        if (from < 0 || from > level) continue; // Not a range, or still locked.
        std::error_code inner;
        for (const auto& nested : std::filesystem::directory_iterator(entry.path(), std::filesystem::directory_options::skip_permission_denied, inner)) take(nested);
    }
    return choose(std::move(files), folder, recent, seed);
}

std::optional<std::vector<uint8_t>> read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<uint64_t>(input.tellg()) > image_limit) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !imageBytes(bytes)) return std::nullopt;
    return bytes;
}

std::wstring description(const std::filesystem::path& file) {
    auto text = file;
    text.replace_extension(L".txt");
    std::ifstream input(text, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 4096) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3); // A BOM from Notepad.
    std::wstring out;
    for (const wchar_t ch : text_encoding::wideLenient(bytes)) out += ch == L'\r' || ch == L'\n' || ch == L'\t' ? L' ' : ch; // One line for the prompt.
    while (!out.empty() && out.back() == L' ') out.pop_back();
    while (!out.empty() && out.front() == L' ') out.erase(0, 1);
    return out;
}
}
