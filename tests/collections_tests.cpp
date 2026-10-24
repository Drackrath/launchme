#include "../src/library/collections.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      (L"launchme-collections-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root / "config");
    const auto selected = root / "userdata" / "39734314" / "config" / "cloudstorage";
    const auto other = root / "userdata" / "39734349" / "config" / "cloudstorage";
    fs::create_directories(selected);
    fs::create_directories(other);
    {
        std::ofstream login(root / "config" / "loginusers.vdf");
        login << "\"users\"\n{\n\"76561198000000077\"\n{\n\"AutoLogin\" \"0\"\n"
              << "\"timestamp\" \"999\"\n}\n\"76561198000000042\"\n{\n"
              << "\"AutoLogin\" \"1\"\n\"timestamp\" \"100\"\n}\n}\n";
    }
    {
        // Steam's favourites are "favorite" (older files: "favorites"). Shortcut IDs are above 2^31,
        // as numbers or strings; a deleted record has no value.
        std::ofstream cloud(selected / "cloud-storage-namespace-1.json");
        cloud << R"([["user-collections.group1",{"value":"{\"name\":\"Moje gry\",\"added\":[10,20,30,10],\"removed\":[20]}"}],)"
              << R"(["user-collections.uc-opaque-id",{"value":"{\"added\":[40]}"}],)"
              << R"(["user-collections.favorite",{"value":"{\"id\":\"favorite\",\"name\":\"Ulubione\",\"added\":[10]}"}],)"
              << R"(["user-collections.favorites",{"value":"{\"name\":\"Ulubione\",\"added\":[10]}"}],)"
              << R"(["user-collections.hidden",{"value":"{\"name\":\"Ukryte\",\"added\":[30]}"}],)"
              << R"(["user-collections.srm-emu",{"value":"{\"name\":\"Emulacja SRM\",\"added\":[3000000000,\"3000000001\",-5,4294967296],\"removed\":[]}"}],)"
              << R"(["user-collections.srm-local",{"value":"{\"name\":\"Tylko lokalne\",\"added\":[],\"removed\":[]}"}],)"
              << R"(["user-collections.srm-gone",{"key":"user-collections.srm-gone","is_deleted":true}]])";
    }
    {
        // Shortcut members kept on this machine only, merged into the synced records by ID.
        std::ofstream local(selected.parent_path() / "localconfig.vdf");
        local << "\"UserLocalConfigStore\"\n{\n\t\"WebStorage\"\n\t{\n\t\t\"user-collections\"\t\t\""
              << R"({\"group1\":{\"id\":\"group1\",\"added\":[3000000002],\"removed\":[]},)"
              << R"(\"srm-emu\":{\"id\":\"srm-emu\",\"added\":[3000000001,3000000003],\"removed\":[3000000000]},)"
              << R"(\"srm-local\":{\"id\":\"srm-local\",\"added\":[4210057888],\"removed\":[]},)"
              << R"(\"srm-gone\":{\"id\":\"srm-gone\",\"added\":[3055926912],\"removed\":[]}})"
              << "\"\r\n\t}\n}\n";
    }
    {
        std::ofstream cloud(other / "cloud-storage-namespace-1.json");
        cloud << R"([["user-collections.other",{"value":"{\"name\":\"Cudze\",\"added\":[99]}"}]])";
    }
    auto collections = readSteamCollections(root);
    if (collections.size() != 4) return 2;
    const auto favorite = std::find_if(collections.begin(), collections.end(), [](const auto& c) { return c.id == "user-collections.favorite"; });
    if (favorite == collections.end() || favorite->app_ids != std::vector<uint32_t>{10}) return 3;
    collections.erase(favorite); // The remaining checks exercise ordinary Steam collections.
    if (collections.size() != 3 || collections[0].name != L"Moje gry" ||
        collections[0].app_ids != std::vector<uint32_t>{10, 30, 3000000002u}) {
        std::cerr << "Collection parsing, account selection, favourites or added/removed failed\n";
        return 1;
    }
    if (collections[1].name != L"Emulacja SRM" || collections[1].app_ids != std::vector<uint32_t>{3000000001u, 3000000003u} ||
        collections[2].name != L"Tylko lokalne" || collections[2].app_ids != std::vector<uint32_t>{4210057888u} ||
        !steamShortcutId(collections[2].app_ids[0]) || steamShortcutId(collections[0].app_ids[0]) ||
        !srmCollection(collections[1]) || srmCollection(collections[0])) {
        std::cerr << "Shortcut IDs or this machine's collection members were not read\n";
        return 1;
    }
    Game shortcut{3000000001u, L"Skrót"};
    shortcut.kind = GameKind::nonsteam;
    const auto emulated = publicSteamShelves({collections[1]}, {shortcut});
    if (emulated.size() != 1 || emulated[0].games.size() != 1 || emulated[0].games[0].app_id != 3000000001u) {
        std::cerr << "A non-Steam shortcut did not appear in its collection\n";
        return 1;
    }
    const auto shelves = publicSteamShelves(collections, {Game{10, L"Publiczna"}, Game{20, L"Removed"}});
    if (shelves.size() != 1 || shelves[0].games.size() != 1 || shelves[0].games[0].app_id != 10) {
        std::cerr << "Private game leaked into a public Steam collection\n";
        return 1;
    }
    Game private_uninstalled{30, L"Prywatna"};
    private_uninstalled.installed = false;
    Game queued{40, L"Jeszcze do instalacji"};
    queued.installed = false;
    const auto with_uninstalled = publicSteamShelves(
        {SteamCollection{L"Kolekcja", {10, 30, 40}}},
        {Game{10, L"Publiczna"}, private_uninstalled, queued}, {30});
    if (with_uninstalled.size() != 1 || with_uninstalled[0].games.size() != 2 ||
        with_uninstalled[0].games[1].app_id != 40 || with_uninstalled[0].games[1].installed) {
        std::cerr << "Uninstalled or hidden collection entries were not separated\n";
        return 1;
    }
    // Ctrl+H: hidden and private games stay in their collections for the session.
    Game secret{30, L"Prywatna"}; secret.visibility = GameVisibility::private_game;
    const auto revealed = publicSteamShelves({SteamCollection{L"Kolekcja", {10, 30}}}, {Game{10, L"Publiczna"}, secret}, {30}, true);
    if (revealed.size() != 1 || revealed[0].games.size() != 2 || revealed[0].games[1].app_id != 30) {
        std::cerr << "Ctrl+H did not show a private game in its collection\n";
        return 1;
    }
}
