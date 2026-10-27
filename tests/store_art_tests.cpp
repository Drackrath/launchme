// Stores' own art (0.3.0): Ubisoft Connect's configurations, Battle.net's public catalog and GOG's
// public product API. Transports are fakes; nothing here reaches a store.
#include "../src/artwork/battlenet_art.hpp"
#include "../src/artwork/gog_art.hpp"
#include "../src/library/ubisoft.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
std::string varint(uint64_t value) {
    std::string out;
    do { uint8_t byte = value & 127; value >>= 7; if (value) byte |= 128; out += static_cast<char>(byte); } while (value);
    return out;
}
std::string field(unsigned id, std::string_view bytes) { return varint((id << 3) | 2) + varint(bytes.size()) + std::string(bytes); }
std::string number(unsigned id, uint64_t value) { return varint(id << 3) + varint(value); }
HttpResponse reply(int status, std::string_view body = {}) {
    HttpResponse response; response.status = status; response.body.assign(body.begin(), body.end()); return response;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-store-art-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto cache = root / L"covers";

    // Ubisoft: a product's root images (asset names only), not a DLC's nor a localization's.
    const std::string yaml = "version: 2.0\r\nroot:\r\n  name: \"Far Cry 3 Blood Dragon\"\r\n"
        "  background_image: 019c77593ec41c6bdcf4b53377a04eb6.jpg\r\n  thumb_image: 52fbec0acc65df17878df15abeff2f19.jpg\r\n"
        "  logo_image: 5d6d4aff77933b40247c251e41416b52.png\r\n  icon_image: 00842e79ba6c2bf42759a65da1e0cc59.ico\r\n"
        "  start_game:\r\n    thumb_image: ffffffffffffffffffffffffffffffff.jpg\r\nlocalizations:\r\n  default:\r\n    thumb_image: eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee.jpg\r\n";
    const std::string bad = "version: 2.0\nroot:\n  thumb_image: ../../evil.jpg\n  logo_image: \"0123456789abcdef0123456789abcdef.png\"\n";
    const auto configurations = field(1, number(1, 157) + number(2, 157) + field(3, "version: 2.0\r\nroot:\r\n  name: ''\r\n")) +
                                field(1, number(1, 101) + number(2, 101) + field(3, yaml)) + field(1, number(1, 55) + field(3, bad));
    const auto art = ubisoft::parseConfigurations(configurations);
    if (art.size() != 2 || art.contains("157")) return 1;
    const auto& fc3 = art.at("101");
    const std::string cdn(ubisoft::asset_cdn);
    if (fc3.thumb != cdn + "52fbec0acc65df17878df15abeff2f19.jpg" || fc3.background != cdn + "019c77593ec41c6bdcf4b53377a04eb6.jpg" ||
        fc3.logo != cdn + "5d6d4aff77933b40247c251e41416b52.png") return 2;
    if (!art.at("55").thumb.empty() || art.at("55").logo != cdn + "0123456789abcdef0123456789abcdef.png") return 3; // Quoted names too.
    if (!ubisoft::parseConfigurations("not protobuf at all").empty()) return 4;
    std::filesystem::create_directories(root / L"ubisoft" / L"configuration");
    { std::ofstream(root / L"ubisoft" / L"configuration" / L"configurations", std::ios::binary) << configurations; }
    if (ubisoft::readConfigurations({root / L"missing", root / L"ubisoft"}).size() != 2) return 5;

    // Battle.net: the newest catalog build, its root, unconditional fragments, products and files.
    const std::string versions = "Region!STRING:0|BuildConfig!HEX:16|CDNConfig!HEX:16\r\n## seqn = 1\r\n"
        "PUB-9|11111111111111111111111111111111|aa\r\nPUB-30|ebc31a1b89d833027ce1df117f548a57|bb\r\nPUB-29|22222222222222222222222222222222|cc\r\n";
    if (battlenetBuildConfig(versions) != "ebc31a1b89d833027ce1df117f548a57" || !battlenetBuildConfig("garbage").empty()) return 6;
    if (battlenetRoot("# Build Configuration\n\nroot = 8ac78282537cb06416e0fc76c283354f\nbuild-version = 30\n") != "8ac78282537cb06416e0fc76c283354f") return 7;
    const std::string root_json = R"({"fragments":[{"hash":"2357f2009055bc1ee43accedcc480209","name":"fenris"},
        {"hash":"3ed05437ccadb72861126784d77b22b1","name":"fenris_cn","requires":{"include":"CN","type":"login_region"}},
        {"hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","name":"btlr"},{"hash":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"auks"},
        {"hash":"cccccccccccccccccccccccccccccccc","name":"kelp"},{"hash":"not-a-hash","name":"broken"}]})";
    const auto fragments = battlenetFragments(root_json);
    if (fragments.size() != 4 || fragments[0].first != "fenris" || fragments[0].second != "2357f2009055bc1ee43accedcc480209") return 8;
    const std::string fenris = R"({"files":{"default":{
        "fenris#FENRIS_KEY_ART":{"hash":"6d1019d2481c18436c9be59b0a578335","name":"resources/fenris/d4_key_art_enUS.jpg"},
        "fenris#FENRIS_BACKGROUND":{"hash":"2e8429d06218b86c03b41accf0b23da7","name":"resources/fenris/background.webp"}},
        "enUS":{"fenris#FENRIS_LOGO":{"hash":"cb6347f2a025f6af5cbede616986b3b3","name":"resources/fenris/logo_fenris.png"}}},
        "products":[{"base":{"program_id":"Fen","key_art":"fenris#FENRIS_KEY_ART","logo":"fenris#FENRIS_LOGO","background":"fenris#FENRIS_BACKGROUND"}}]})";
    const auto cod = [](const char* frag, const char* hash) {
        return std::string(R"({"files":{"default":{")") + frag + R"(#KEY_ART":{"hash":")" + hash + R"(","name":"k.jpg"}}},"products":[{"base":{"program_id":"AUKS","key_art":")" + frag + R"(#KEY_ART"}}]})";
    };
    const std::vector<std::pair<std::string, std::string>> bodies{{"fenris", fenris}, {"btlr", cod("btlr", "11111111111111111111111111111111")},
        {"auks", cod("auks", "33333333333333333333333333333333")}, {"kelp", cod("kelp", "44444444444444444444444444444444")}};
    const auto catalog = battlenetCatalogArt(bodies);
    if (catalog.at("fen").tall != battlenetDataUrl("6d1019d2481c18436c9be59b0a578335") || catalog.at("fen").logo != battlenetDataUrl("cb6347f2a025f6af5cbede616986b3b3")) return 9;
    if (catalog.at("auks").tall != battlenetDataUrl("33333333333333333333333333333333")) return 10; // Several claims: its own fragment.
    if (battlenetDataUrl("6d1019d2481c18436c9be59b0a578335") != "https://us.cdn.blizzard.com/tpr/catalogs/data/6d/10/6d1019d2481c18436c9be59b0a578335") return 11;
    // The walk once, then the saved catalog; a failure is not saved.
    int calls = 0;
    std::map<std::wstring, std::string> served{
        {L"https://us.version.battle.net/v2/products/catalogs/versions", versions},
        {L"https://us.cdn.blizzard.com/tpr/catalogs/config/eb/c3/ebc31a1b89d833027ce1df117f548a57", "root = 8ac78282537cb06416e0fc76c283354f\n"},
        {L"https://us.cdn.blizzard.com/tpr/catalogs/data/8a/c7/8ac78282537cb06416e0fc76c283354f", root_json},
        {L"https://us.cdn.blizzard.com/tpr/catalogs/data/23/57/2357f2009055bc1ee43accedcc480209", fenris}};
    const auto blizzard = [&](const std::wstring& url, const std::wstring&, size_t) {
        ++calls;
        const auto found = served.find(url);
        return found == served.end() ? reply(404) : reply(200, found->second);
    };
    if (battlenetArtUrl("Fen", false, cache, blizzard) != battlenetDataUrl("6d1019d2481c18436c9be59b0a578335")) return 12;
    const int walked = calls;
    if (battlenetArtUrl("FEN", true, cache, blizzard) != battlenetDataUrl("cb6347f2a025f6af5cbede616986b3b3") || calls != walked) return 13;
    if (!battlenetArtUrl("WoW", false, cache, blizzard).empty() || calls != walked) return 14;
    if (!std::filesystem::exists(root / L"artwork" / L"battlenet-art.v1")) return 15;

    // GOG: box art, the Galaxy background (else the plain one), the logo; WebP and http links are left out.
    const std::string product = R"({"_links":{"logo":{"href":"https://images.gog-statics.com/logo.png"},
        "boxArtImage":{"href":"//images.gog-statics.com/box.jpg"},"backgroundImage":{"href":"https://images.gog-statics.com/bg.jpg"},
        "galaxyBackgroundImage":{"href":"https://images.gog-statics.com/galaxy.webp"}}})";
    const auto gog = parseGogArt(product);
    if (gog.tall != "https://images.gog-statics.com/box.jpg" || gog.wide != "https://images.gog-statics.com/bg.jpg" || gog.logo != "https://images.gog-statics.com/logo.png") return 16;
    if (!parseGogArt("{}").tall.empty() || !parseGogArt(R"({"_links":{"logo":{"href":"http://x/logo.png"}}})").logo.empty()) return 17;
    int gog_calls = 0;
    int status = 503;
    const auto gogGet = [&](const std::wstring& url, const std::wstring&, size_t) {
        ++gog_calls;
        if (url.find(L"https://api.gog.com/v2/games/1207658924") != 0) return reply(500);
        return status == 200 ? reply(200, product) : reply(status);
    };
    if (!gogArt("1207658924", cache, gogGet).tall.empty() || gog_calls != 1) return 18; // Unanswered: nothing saved.
    status = 200;
    if (gogArt("1207658924", cache, gogGet).wide != gog.wide || gog_calls != 2) return 19;
    if (gogArt("1207658924", cache, gogGet).logo != gog.logo || gog_calls != 2) return 20; // From its file.
    if (!gogArt("../1", cache, gogGet).tall.empty() || gog_calls != 2) return 21;

    std::filesystem::remove_all(root, ec);
    std::cout << "Ubisoft configurations art, Battle.net catalog art and GOG product art passed\n";
    return 0;
}
