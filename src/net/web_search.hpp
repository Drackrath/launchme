#pragma once

// Towarzyszka's optional web search (Tavily, https://tavily.com): offered to the model only when a key
// is there, either saved in Ustawienia (encrypted for this Windows account) or as TAVILY_API_KEY.
#include "json.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace web_search {

inline constexpr const wchar_t* environment = L"TAVILY_API_KEY";

std::wstring storedKey(const std::filesystem::path& config);                     // The saved one only.
bool saveKey(const std::filesystem::path& config, std::wstring_view key);         // Empty: forgets it.
std::wstring key(const std::filesystem::path& config, bool* from_environment = nullptr); // Saved, else the variable.

// A few results for the query, with Tavily's short answer when it gives one: {"answer", "results":
// [{"title", "url", "content"}]}, or {"error"} for the model to tell. Blocks; the caller's thread.
json::Value search(const std::wstring& key, const std::wstring& query);

} // namespace web_search
