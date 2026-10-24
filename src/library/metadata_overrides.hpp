#pragma once

// The player's own metadata for a game (desktop mode's "Edytuj metadane…"): wherever a field is set it
// wins over what the stores, RAWG, ScreenScraper or the game index found, e.g. a PC port matched as
// the console game it was made from. Kept by game key in metadata_overrides.json.
#include "../model/game_details.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace metadata {

struct Override {
    std::wstring title, description, developer, publisher, release_date, genres; // Empty: automatic.
    std::wstring hours; // How long the main story takes (as HowLongToBeat), as typed: "22", "20-24", "7,5".
    bool looked_up = false; // Its title's lookups were made afresh (HowLongToBeat, RAWG under the new name).
    bool empty() const {
        return title.empty() && description.empty() && developer.empty() && publisher.empty() && release_date.empty() && genres.empty() && hours.empty();
    }
};

class Overrides {
public:
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    const Override* find(const std::string& key) const;
    void set(const std::string& key, Override value); // An empty one is forgotten.
    bool erase(const std::string& key);
    const std::map<std::string, Override>& items() const { return items_; }

private:
    std::map<std::string, Override> items_;
};

// The set fields over the found details.
void apply(const Override& value, GameDetails& details);

} // namespace metadata
