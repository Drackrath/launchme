#pragma once

#include "../model/game.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct StoreClient {
    GameSource source = GameSource::steam;
    std::filesystem::path executable;
};
struct StoreLibrary {
    std::vector<Game> visible;
    std::vector<Game> private_games;
    std::vector<std::wstring> errors;
    std::vector<StoreClient> clients;
    bool battlenet_available = true;
    // Ubisoft Connect: an account's ownership cache was read; owned products still without a
    // title as {product ID, catalogue UUID (may be empty)}.
    bool ubisoft_account = false;
    std::vector<std::pair<std::string, std::string>> ubisoft_unnamed;
};

// Import and action planning are independent of ShellExecute and can be tested offline.
// The first provider is a versioned local catalog; native client readers can replace
// scan() without changing the game model, rendering or artwork workers.
class StoreAdapter {
public:
    virtual ~StoreAdapter() = default;
    virtual GameSource source() const = 0;
    virtual StoreLibrary scan(const std::filesystem::path& directory) const = 0;
    virtual GameAction action(const Game& game) const = 0;
};

std::unique_ptr<StoreAdapter> makeStoreAdapter(GameSource source);
StoreLibrary parseStoreCatalog(GameSource source, std::string_view bytes);
StoreLibrary scanStoreLibraries(const std::filesystem::path& directory);
GameAction primaryGameAction(const Game& game);
void configureStoreActions(Game& game, const std::wstring& client, const std::wstring& install_directory,
                           const std::string& epic_namespace = {}, const std::string& epic_item = {});
bool validStoreGameId(GameSource source, const std::string& id);
