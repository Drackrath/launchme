#include "game_details.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../net/http.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <chrono>
#include <fstream>

namespace {
std::wstring plainText(std::string text) {
    std::string plain;
    bool tag = false;
    for (char ch : text) {
        if (ch == '<') { tag = true; if (!plain.empty() && plain.back() != ' ') plain += ' '; }
        else if (ch == '>') tag = false;
        else if (!tag) plain += ch;
    }
    for (const auto& [entity, replacement] : {std::pair{"&amp;", "&"}, {"&quot;", "\""},
         {"&#39;", "'"}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}}) {
        size_t position = 0;
        while ((position = plain.find(entity, position)) != std::string::npos) {
            plain.replace(position, std::char_traits<char>::length(entity), replacement);
            position += std::char_traits<char>::length(replacement);
        }
    }
    return text_encoding::wide(plain);
}
std::wstring arrayText(const json::Value* value, const char* property = nullptr) {
    std::wstring result;
    if (!value || value->type != json::Value::Type::array) return result;
    for (const auto& item : value->elements) {
        const auto* label = property ? item.get(property) : &item;
        if (!label) continue;
        const auto text = plainText(label->stringOr());
        if (text.empty()) continue;
        if (!result.empty()) result += L", ";
        result += text;
        if (result.size() > 250) break;
    }
    return result;
}
}

GameDetails parseGameDetails(uint32_t app_id, std::string_view bytes) {
    GameDetails result;
    const auto root = json::parse(bytes);
    const auto* entry = root ? root->get(std::to_string(app_id)) : nullptr;
    const auto* success = entry ? entry->get("success") : nullptr;
    const auto* data = entry ? entry->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::object || !success || success->type != json::Value::Type::boolean || !success->flag) {
        result.error = tr(L"Opis tej gry nie jest teraz dostępny.");
        return result;
    }
    const auto* description = data->get("short_description");
    result.description = description ? plainText(description->stringOr()) : std::wstring{};
    if (result.description.size() > 1800) result.description.resize(1800);
    result.developer = arrayText(data->get("developers"));
    result.publisher = arrayText(data->get("publishers"));
    result.genres = arrayText(data->get("genres"), "description");
    const auto* release = data->get("release_date");
    const auto* date = release ? release->get("date") : nullptr;
    result.release_date = date ? plainText(date->stringOr()) : std::wstring{};
    return result;
}

GameDetails fetchGameDetails(uint32_t app_id, const std::filesystem::path& cache) {
    if (!app_id) return {};
    // Steam localizes the description and dates; each UI language has its own cached copy.
    const auto language = i18n::language();
    const auto file = cache / (std::to_wstring(app_id) + (language == i18n::Language::en ? L".en.details.json" :
        language == i18n::Language::pt_br ? L".pt-BR.details.json" : language == i18n::Language::es ? L".es.details.json" : L".details.json"));
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(file, ec);
    std::string saved;
    if (!ec) {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (input && input.tellg() > 0 && input.tellg() <= 2 * 1024 * 1024) {
            saved.resize(static_cast<size_t>(input.tellg()));
            input.seekg(0);
            if (!input.read(saved.data(), static_cast<std::streamsize>(saved.size()))) saved.clear();
        }
        if (!saved.empty() && std::filesystem::file_time_type::clock::now() - modified < std::chrono::hours(24)) {
            const auto result = parseGameDetails(app_id, saved);
            if (result.error.empty()) return result;
        }
    }
    const auto response = httpGet(L"https://store.steampowered.com/api/appdetails?appids=" + std::to_wstring(app_id) +
                                 L"&l=" + std::wstring(i18n::steamLanguage()), L"Accept: application/json\r\n", 2 * 1024 * 1024);
    const auto text = std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size());
    auto result = parseGameDetails(app_id, text);
    if (response.status == 200 && result.error.empty()) atomic_file::write(file, text);
    else if (!saved.empty()) result = parseGameDetails(app_id, saved);
    return result;
}
