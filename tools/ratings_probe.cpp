// Manual, opt-in API probe. The user's key is read locally and never printed.
#include "../src/library/game_ratings.hpp"
#include "../src/library/rawg.hpp"
#include "../src/artwork/identified_artwork.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/net/json.hpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::wstring_view(argv[2]) != L"--artwork")) {
        std::cerr << "Usage: ratings_probe <RAWG game ID> [--artwork]\n"; return 2;
    }
    const std::wstring id = argv[1];
    if (id.empty() || id.find_first_not_of(L"0123456789") != id.npos) return 2;
    const auto key = userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt");
    if (key.empty() || key.find_first_not_of(L"0123456789abcdefABCDEF") != key.npos) { std::cerr << "RAWG key missing or invalid\n"; return 3; }
    HttpBudget budget(argc == 3 ? 35000 : 10000);
    const auto response = httpGetPrivate(L"https://api.rawg.io/api/games/" + id + L"?key=" + key, L"Accept: application/json\r\n", 512 * 1024);
    std::cout << "RAWG HTTP " << response.status << "; bytes=" << response.body.size() << '\n';
    if (response.status != 200) return 4;
    const auto root = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    if (!root || root->type != json::Value::Type::object) return 5;
    for (const auto* field : {"name", "slug", "released", "description_raw", "developers", "publishers", "genres", "tags", "platforms", "rating", "ratings_count", "metacritic", "background_image", "background_image_additional", "website", "stores"}) {
        const auto* value = root->get(field);
        std::cout << field << ": ";
        if (!value || value->type == json::Value::Type::null_value) std::cout << "absent";
        else if (value->type == json::Value::Type::string) {
            if (std::string_view(field) == "name" || std::string_view(field) == "released" || std::string_view(field) == "slug") std::cout << value->text;
            else std::cout << value->text.size() << " bytes";
        } else if (value->type == json::Value::Type::array) {
            std::cout << value->elements.size() << " entries";
            if (std::string_view(field) == "developers" || std::string_view(field) == "publishers" || std::string_view(field) == "genres")
                for (const auto& entry : value->elements) if (const auto* name = entry.get("name")) std::cout << " | " << name->stringOr();
        } else if (value->type == json::Value::Type::number) std::cout << value->numeric;
        else std::cout << "present";
        std::cout << '\n';
    }
    if (argc == 3) {
        const auto info = rawg::parseDetails(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
        if (!info) return 6;
        const auto links = rawg::stores(info->id, key);
        std::cout << "RAWG store links valid=" << links.valid << "; Steam AppID=" << links.steam_app_id << '\n';
        Game game; game.kind = GameKind::store; game.source = GameSource::custom;
        game.source_id = "rawg-probe-" + std::to_string(info->id); game.name = info->name;
        const auto cache = std::filesystem::temp_directory_path() / L"opencode" / (L"rawg-artwork-" + id) / L"covers";
        const auto art = fillIdentifiedArtwork(game, info->name, links.steam_app_id, cache,
            userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"), [] { return true; });
        std::cout << "Missing=" << art.missing << "; filled=" << art.images.size() << "; status=" << text_encoding::utf8(art.error) << '\n';
        for (const auto& item : art.images)
            std::cout << text_encoding::utf8(artworkKindSlug(item.kind)) << ": " << item.bytes.size() << " bytes\n";
        if (art.images.empty() && art.missing) {
            const auto candidates = searchGridGames(info->name, userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"));
            for (const auto& candidate : candidates.games)
                std::cout << "SGDB candidate " << candidate.source.id << ": " << text_encoding::utf8(candidate.name)
                          << "; score=" << gridTitleScore(info->name, candidate.name) << '\n';
        }
        return art.error.empty() ? 0 : 7;
    }
    return 0;
}
