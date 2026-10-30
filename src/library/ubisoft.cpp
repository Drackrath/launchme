#include "ubisoft.hpp"
#include "../platform/file_io.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <set>

namespace ubisoft {
namespace {
constexpr size_t max_cache_bytes = 4 * 1024 * 1024;
struct Field { unsigned id = 0, wire = 0; uint64_t number = 0; std::string_view bytes; };

// Plain protobuf fields. Groups and malformed lengths reject the whole buffer.
std::optional<std::vector<Field>> decode(std::string_view bytes) {
    std::vector<Field> fields;
    size_t cursor = 0;
    const auto varint = [&]() -> std::optional<uint64_t> {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 64 && cursor < bytes.size(); shift += 7) {
            const auto byte = static_cast<unsigned char>(bytes[cursor++]);
            if (shift == 63 && byte > 1) return {};
            value |= static_cast<uint64_t>(byte & 127) << shift;
            if (!(byte & 128)) return value;
        }
        return {};
    };
    while (cursor < bytes.size()) {
        if (fields.size() >= 20000) return {};
        const auto tag = varint();
        if (!tag || !(*tag >> 3) || (*tag >> 3) > 0x1fffffff) return {};
        Field field{static_cast<unsigned>(*tag >> 3), static_cast<unsigned>(*tag & 7)};
        if (!field.wire) {
            const auto value = varint();
            if (!value) return {};
            field.number = *value;
        } else {
            const auto size = field.wire == 2 ? varint() : field.wire == 1 ? std::optional<uint64_t>{8} :
                              field.wire == 5 ? std::optional<uint64_t>{4} : std::nullopt;
            if (!size || *size > bytes.size() - cursor) return {};
            field.bytes = bytes.substr(cursor, static_cast<size_t>(*size));
            cursor += static_cast<size_t>(*size);
        }
        fields.push_back(field);
    }
    return fields;
}
uint32_t littleEndian(std::string_view bytes, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
std::string readFile(const std::filesystem::path& path, size_t limit) { return file_io::readText(path, limit); }
bool decimalId(std::string_view text) {
    return !text.empty() && text.size() <= 10 && text.front() != '0' &&
           std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}
std::string lower(std::string_view text) {
    std::string result(text);
    for (char& ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return result;
}
// Display text only: bounded, no control characters, trademark signs removed.
std::wstring cleanTitle(std::wstring name) {
    std::erase_if(name, [](wchar_t ch) { return ch == L'®' || ch == L'™' || ch == L'©'; });
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    if (name.size() > 200 || std::any_of(name.begin(), name.end(), [](wchar_t ch) { return ch < 0x20 || ch == 0x7f; })) return {};
    return name;
}
bool validKey(std::string_view key) { return validUuid(key) || decimalId(key); }
}

bool validUuid(std::string_view text) {
    if (text.size() != 36) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (ch != '-') return false; continue; }
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}

std::optional<std::vector<Product>> parseOwnership(std::string_view bytes) {
    if (bytes.size() < 8 || bytes.size() > max_cache_bytes) return {};
    const auto signature = littleEndian(bytes, 0);
    if (signature > 4096 || bytes.size() < 8ull + signature) return {};
    if (littleEndian(bytes, 4 + signature) != bytes.size() - 8 - signature) return {};
    const auto top = decode(bytes.substr(8 + signature));
    if (!top) return {};
    std::vector<Product> result;
    std::set<std::string> seen;
    for (const auto& field : *top) {
        if (field.id != 1) continue;
        if (field.wire != 2) return {};
        const auto record = decode(field.bytes);
        if (!record) return {};
        uint64_t id = 0;
        bool addon = false;
        std::string uuid;
        for (const auto& value : *record) {
            if (value.id == 1 && value.wire == 0) id = value.number;
            else if (value.id == 6 && value.wire == 0) addon = value.number != 0;
            else if (value.id == 19 && value.wire == 2 && validUuid(value.bytes)) uuid = lower(value.bytes);
            // Field 4 may be a CD key; it is deliberately never read.
        }
        if (!id || id > 0xffffffffull || addon) continue;
        auto text = std::to_string(id);
        if (seen.insert(text).second) result.push_back({std::move(text), std::move(uuid)});
    }
    return result;
}

std::map<std::string, uint64_t> parseRecentlyPlayed(std::string_view bytes) {
    std::map<std::string, uint64_t> result;
    if (bytes.size() > max_cache_bytes) return result;
    const auto top = decode(bytes);
    if (!top) return result;
    for (const auto& field : *top) {
        if (field.id != 4 || field.wire != 2) continue;
        const auto list = decode(field.bytes);
        if (!list) continue;
        for (const auto& item : *list) {
            if (item.id != 1 || item.wire != 2) continue;
            const auto entry = decode(item.bytes);
            if (!entry) continue;
            uint64_t id = 0, time = 0;
            for (const auto& value : *entry) {
                if (value.id == 1 && value.wire == 0) id = value.number;
                else if (value.id == 3 && value.wire == 0) time = value.number;
            }
            // Unix seconds between 2001 and 2100; anything else is not a timestamp.
            if (!id || id > 0xffffffffull || time < 1000000000ull || time > 4102444800ull) continue;
            auto& played = result[std::to_string(id)];
            played = std::max(played, time);
        }
    }
    return result;
}

std::map<std::string, Art> parseConfigurations(std::string_view bytes) {
    std::map<std::string, Art> result;
    if (bytes.size() > max_configuration_bytes) return result;
    const auto top = decode(bytes);
    if (!top) return result;
    // An asset file name: 32 hex digits and .jpg/.png (nothing else is joined to the CDN address).
    const auto asset = [](std::string_view value) -> std::string {
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '"' || value.back() == '\'')) value.remove_suffix(1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '"' || value.front() == '\'')) value.remove_prefix(1);
        if (value.size() != 36 || !(value.ends_with(".jpg") || value.ends_with(".png"))) return {};
        if (!std::all_of(value.begin(), value.begin() + 32, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); })) return {};
        return std::string(asset_cdn) + std::string(value);
    };
    for (const auto& field : *top) {
        if (field.id != 1 || field.wire != 2) continue;
        const auto record = decode(field.bytes);
        if (!record) continue;
        uint64_t id = 0;
        std::string_view yaml;
        for (const auto& value : *record) {
            if (value.id == 1 && value.wire == 0) id = value.number;
            else if (value.id == 3 && value.wire == 2) yaml = value.bytes;
        }
        if (!id || id > 0xffffffffull || yaml.empty()) continue;
        // The product's own images: two-space keys under "root:" (localizations and DLC lists are deeper).
        Art art;
        bool root = false;
        size_t start = 0;
        while (start < yaml.size()) {
            auto end = yaml.find('\n', start);
            if (end == std::string_view::npos) end = yaml.size();
            const auto line = yaml.substr(start, end - start);
            start = end + 1;
            if (!line.empty() && line.front() != ' ') { root = line.starts_with("root:"); continue; }
            if (!root || !line.starts_with("  ") || line.starts_with("   ")) continue;
            const auto text = line.substr(2);
            if (text.starts_with("thumb_image:")) art.thumb = asset(text.substr(12));
            else if (text.starts_with("background_image:")) art.background = asset(text.substr(17));
            else if (text.starts_with("logo_image:")) art.logo = asset(text.substr(11));
        }
        if (!art.thumb.empty() || !art.background.empty() || !art.logo.empty()) result[std::to_string(id)] = std::move(art);
    }
    return result;
}

std::map<std::string, Art> readConfigurations(const std::vector<std::filesystem::path>& caches) {
    for (const auto& cache : caches) {
        auto art = parseConfigurations(readFile(cache / L"configuration" / L"configurations", max_configuration_bytes));
        if (!art.empty()) return art;
    }
    return {};
}

std::optional<Account> readAccount(const std::vector<std::filesystem::path>& caches) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    for (const auto& cache : caches) {
        std::error_code ec;
        size_t count = 0;
        for (std::filesystem::directory_iterator it(cache / L"ownership", ec), end; !ec && it != end && count++ < 64; it.increment(ec)) {
            const auto name = text_encoding::utf8(it->path().filename().wstring());
            if (!validUuid(name) || !it->is_regular_file(ec)) continue;
            const auto time = it->last_write_time(ec);
            if (!ec && (newest.empty() || time > newest_time)) { newest = it->path(); newest_time = time; }
        }
    }
    if (newest.empty()) return {};
    auto products = parseOwnership(readFile(newest, max_cache_bytes));
    if (!products) return {};
    Account account;
    account.id = lower(text_encoding::utf8(newest.filename().wstring()));
    account.products = std::move(*products);
    account.last_played = parseRecentlyPlayed(readFile(newest.parent_path().parent_path() / L"settings" / newest.filename(), max_cache_bytes));
    return account;
}

std::map<std::string, std::wstring> parseNames(std::string_view text) {
    std::map<std::string, std::wstring> names;
    while (!text.empty() && names.size() < 20000) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        const auto key = lower(line.substr(0, tab));
        auto name = cleanTitle(text_encoding::wide(line.substr(tab + 1)));
        if (validKey(key) && !name.empty()) names[key] = std::move(name);
    }
    return names;
}

std::string serializeNames(const std::map<std::string, std::wstring>& names) {
    std::string text;
    for (const auto& [key, name] : names) {
        const auto clean = cleanTitle(name);
        if (validKey(key) && !clean.empty()) text += key + '\t' + text_encoding::utf8(clean) + '\n';
    }
    return text;
}

std::map<std::string, std::wstring> loadNames(const std::filesystem::path& file) {
    return file.empty() ? std::map<std::string, std::wstring>{} : parseNames(readFile(file, 1024 * 1024));
}

std::map<std::string, std::wstring> parseUuidCatalog(std::string_view text) {
    std::map<std::string, std::wstring> names;
    const auto root = json::parse(text);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::object) return names;
    for (const auto& [key, entry] : games->properties) {
        if (!validUuid(key) || entry.type != json::Value::Type::object) continue;
        const auto* name = entry.get("name");
        auto title = name ? cleanTitle(text_encoding::wideLenient(name->stringOr())) : std::wstring{};
        if (!title.empty()) names[lower(key)] = std::move(title);
    }
    return names;
}

std::map<std::string, std::wstring> parseIdList(std::string_view text) {
    std::map<std::string, std::wstring> names;
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto comma = line.find(',');
        if (comma == std::string_view::npos) continue;
        const auto id = line.substr(0, comma);
        auto title = cleanTitle(text_encoding::wideLenient(line.substr(comma + 1)));
        // The first entry wins: later rows of an ID are editions/QC builds of the same product.
        if (decimalId(id) && !title.empty()) names.try_emplace(std::string(id), std::move(title));
    }
    return names;
}

std::wstring title(const std::map<std::string, std::wstring>& names, const Product& product) {
    if (!product.uuid.empty()) if (const auto found = names.find(product.uuid); found != names.end()) return found->second;
    const auto found = names.find(product.id);
    return found == names.end() ? std::wstring{} : found->second;
}

std::vector<Product> unnamed(const std::map<std::string, std::wstring>& names, const std::vector<Product>& products) {
    std::vector<Product> result;
    for (const auto& product : products) if (title(names, product).empty()) result.push_back(product);
    return result;
}
}
