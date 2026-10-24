#pragma once
#include "../net/http.hpp"
#include <functional>
#include <string_view>

struct GridSource { uint32_t id = 0; bool steam = false; };
struct GridGame { GridSource source; std::wstring name; int year = 0; }; // year: SGDB release date, 0 = unknown.
struct GridSearch { std::vector<GridGame> games; std::wstring error; };
struct GridMatch { GridSource source; std::wstring error; };
using ArtworkHttp = std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)>;

// Shared by automatic artwork and the manual browser. Scores never use store IDs.
int gridTitleScore(std::wstring_view title, std::wstring_view candidate);
GridSource matchGridGame(std::wstring_view title, const std::vector<GridGame>& candidates);
GridSearch searchGridGames(const std::wstring& query, const std::wstring& key, const ArtworkHttp& get = httpGet);
GridMatch resolveGridGame(const std::wstring& title, const std::wstring& key, const ArtworkHttp& get = httpGet);
