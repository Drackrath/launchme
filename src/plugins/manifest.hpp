#pragma once
#include "json.hpp"
#include "../model/game.hpp"
#include <optional>

namespace plugins {
struct Text {
    std::wstring pl, en;
    std::wstring localized() const { return i18n::language() == i18n::Language::en ? en : pl; }
};
struct Setting {
    std::string id, type;
    Text title;
    json::Value initial;
    std::vector<std::pair<std::string, Text>> choices;
    bool accepts(const json::Value& value) const;
};
struct Contribution {
    std::string id, command, source, kind; // kind: menu, context, widget, catalog, details, library
    Text title, text;
    bool active_game = false; // Widget requests/activation get the current running game.
    std::string glyph; // Optional native quick-bar symbol: store (default), server-download.
};
struct Manifest {
    std::string id, version, entry;
    Text name, description;
    std::vector<Setting> settings;
    std::vector<Contribution> contributions;
    std::vector<std::string> tags;
    bool library = false; // Public game identity/title snapshots in command/widget requests.
    std::string icon; // Optional package-local PNG, rendered as a circle.
    std::wstring icon_text; // Optional 1-3 character monogram / image fallback.
};
bool identifier(std::string_view value);
bool packagePath(std::string_view value);
Manifest parseManifest(std::string_view bytes); // throws descriptive std::runtime_error
bool matches(const Contribution& item, const Game& game);
std::string gameJson(const Game& game);
struct CatalogResult { std::vector<Game> games; std::wstring error; };
CatalogResult parseCatalog(const json::Value& value, const std::string& owner);
std::optional<std::vector<uint32_t>> parseLibrarySupplement(const json::Value& value);
}
