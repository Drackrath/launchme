#include "web_search.hpp"
#include "http.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"

#include <Windows.h>

namespace web_search {
namespace {
std::filesystem::path keyFile(const std::filesystem::path& config) { return config / L"companion_tavily_key.dat"; }

json::Value failure(std::string message) {
    auto out = json::object();
    out.properties["error"] = json::string(std::move(message));
    return out;
}

// At most `limit` bytes of UTF-8, never splitting a character.
std::string clipped(std::string text, size_t limit) {
    if (text.size() <= limit) return text;
    size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.resize(end);
    return text + "…";
}
} // namespace

std::wstring storedKey(const std::filesystem::path& config) {
    const auto text = protected_file::read(keyFile(config), 4096);
    if (!text || text->empty()) return {};
    auto key = text_encoding::wideLenient(*text);
    while (!key.empty() && (key.back() == L'\n' || key.back() == L'\r' || key.back() == L' ')) key.pop_back();
    return key;
}

bool saveKey(const std::filesystem::path& config, std::wstring_view key) {
    while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) key.remove_suffix(1);
    while (!key.empty() && (key.front() == L' ' || key.front() == L'\t')) key.remove_prefix(1);
    if (key.empty()) { std::error_code error; std::filesystem::remove(keyFile(config), error); return !error; }
    std::error_code error;
    std::filesystem::create_directories(config, error);
    return protected_file::write(keyFile(config), text_encoding::utf8Lenient(key), L"Launchme Tavily key");
}

std::wstring key(const std::filesystem::path& config, bool* from_environment) {
    if (from_environment) *from_environment = false;
    if (auto saved = storedKey(config); !saved.empty()) return saved;
    wchar_t value[1025]{};
    const DWORD length = GetEnvironmentVariableW(environment, value, static_cast<DWORD>(std::size(value)));
    if (!length || length >= std::size(value)) return {};
    if (from_environment) *from_environment = true;
    return std::wstring(value, length);
}

json::Value search(const std::wstring& key, const std::wstring& query) {
    if (key.empty()) return failure("Web search has no key.");
    if (query.empty()) return failure("zapytanie is required");
    auto request = json::object();
    request.properties["query"] = json::string(text_encoding::utf8Lenient(query.substr(0, 400)));
    request.properties["max_results"] = json::number(5);
    request.properties["search_depth"] = json::string("basic");
    request.properties["include_answer"] = json::boolean(true);
    const auto headers = L"Content-Type: application/json\r\nAccept: application/json\r\nAuthorization: Bearer " + key + L"\r\n";
    const auto reply = httpPostLong(L"https://api.tavily.com/search", headers, json::serialize(request), 1024 * 1024, 20000);
    if (reply.status == 401 || reply.status == 403) return failure("The web search key was refused.");
    if (reply.status == 429 || reply.status == 432) return failure("The web search allowance is used up for now.");
    if (reply.status != 200) return failure(reply.status ? "Web search failed (HTTP " + std::to_string(reply.status) + ")." : "Web search could not connect.");
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
    if (!parsed) return failure("Web search answered with something unreadable.");
    auto out = json::object();
    if (const auto* answer = parsed->get("answer")) if (auto text = answer->stringOr(); !text.empty()) out.properties["answer"] = json::string(clipped(std::move(text), 1200));
    auto results = json::array();
    if (const auto* found = parsed->get("results")) {
        for (const auto& result : found->elements) {
            if (results.elements.size() >= 5) break;
            const auto field = [&](std::string_view name) { const auto* value = result.get(name); return value ? value->stringOr() : std::string{}; };
            auto item = json::object();
            item.properties["title"] = json::string(clipped(field("title"), 200));
            item.properties["url"] = json::string(clipped(field("url"), 300));
            item.properties["content"] = json::string(clipped(field("content"), 700));
            results.elements.push_back(std::move(item));
        }
    }
    out.properties["results"] = std::move(results);
    return out;
}

} // namespace web_search
