#pragma once
#include "collections.hpp"
#include <map>
#include <set>

// Local overlays never change Steam's files. Steam IDs are scoped to their account;
// local collection IDs and favourites of other stores survive a Steam account change.
struct CollectionEdits {
    std::map<std::string, std::wstring> names;
    std::map<std::string, std::map<std::string, bool>> members;
    std::set<std::string> creates; // Account-scoped Steam collections created offline, awaiting native confirmation.
    bool operator==(const CollectionEdits&) const = default;
};
inline bool favoriteCollection(const SteamCollection& c) { return c.id == "user-collections.favorite"; }
inline bool localCollection(const SteamCollection& c) { return c.id.starts_with("local:"); }
std::vector<SteamCollection> managedCollections(uint64_t account, const std::vector<SteamCollection>& steam, const CollectionEdits& edits);
bool collectionContains(const CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game);
void setCollectionMember(CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game, bool member);
std::vector<uint32_t> localSteamMembers(const CollectionEdits& edits, uint64_t account);
std::vector<SteamShelf> managedShelves(uint64_t account, const std::vector<SteamCollection>& collections,
    const CollectionEdits& edits, const std::vector<Game>& visible, bool include_private = false);
