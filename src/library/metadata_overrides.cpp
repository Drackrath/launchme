#include "metadata_overrides.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <fstream>
#include <span>

namespace metadata {
namespace {
constexpr size_t max_file = 4 * 1024 * 1024;
const std::pair<const char*, std::wstring Override::*> fields[] = {
    {"title", &Override::title}, {"description", &Override::description}, {"developer", &Override::developer},
    {"publisher", &Override::publisher}, {"release_date", &Override::release_date}, {"genres", &Override::genres}, {"hours", &Override::hours},
};
} // namespace

bool Overrides::load(const std::filesystem::path& file) {
    items_.clear();
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return true; // None yet.
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(max_file)) return false;
    std::string text(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return false;
    const auto root = json::parse(text, max_file);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games) return false;
    for (const auto& [key, entry] : games->properties) {
        Override value;
        for (const auto& [name, member] : fields)
            if (const auto* field = entry.get(name)) value.*member = text_encoding::wideLenient(field->stringOr());
        if (const auto* flag = entry.get("looked_up")) value.looked_up = flag->type == json::Value::Type::boolean && flag->flag;
        if (!key.empty() && !value.empty()) items_[key] = std::move(value);
    }
    return true;
}

bool Overrides::save(const std::filesystem::path& file) const {
    auto games = json::object();
    for (const auto& [key, value] : items_) {
        auto entry = json::object();
        for (const auto& [name, member] : fields)
            if (!(value.*member).empty()) entry.properties[name] = json::string(text_encoding::utf8Lenient(value.*member));
        if (value.looked_up) entry.properties["looked_up"] = json::boolean(true);
        games.properties[key] = std::move(entry);
    }
    auto root = json::object();
    root.properties["version"] = json::number(1);
    root.properties["games"] = std::move(games);
    const auto text = json::serialize(root);
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return atomic_file::write(file, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

const Override* Overrides::find(const std::string& key) const {
    const auto found = items_.find(key);
    return found == items_.end() ? nullptr : &found->second;
}

void Overrides::set(const std::string& key, Override value) {
    if (key.empty()) return;
    if (value.empty()) items_.erase(key);
    else items_[key] = std::move(value);
}

bool Overrides::erase(const std::string& key) { return items_.erase(key) > 0; }

void apply(const Override& value, GameDetails& details) {
    if (!value.description.empty()) details.description = value.description;
    if (!value.developer.empty()) details.developer = value.developer;
    if (!value.publisher.empty()) details.publisher = value.publisher;
    if (!value.release_date.empty()) details.release_date = value.release_date;
    if (!value.genres.empty()) details.genres = value.genres;
}

} // namespace metadata
