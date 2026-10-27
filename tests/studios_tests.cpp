// Publishers and developers with a logo (src/library/studios.hpp): folded store names, the
// catalog's own consistency (every logo shipped, ids unique), matching by name and by leading
// words, the longest match winning, and the grouping of a library.
#include "../src/library/studios.hpp"

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <set>

#define CHECK(value) do { if (!(value)) { std::cerr << "Studios check failed at line " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
    using namespace studios;
    // ---- Folding.
    CHECK(fold(L"CAPCOM Co., Ltd.") == L"capcom");
    CHECK(fold(L"Ubisoft Entertainment SA") == L"ubisoft entertainment");
    CHECK(fold(L"CD PROJEKT S.A.") == L"cd projekt");
    CHECK(fold(L"11 bit studios S.A.") == L"11 bit studios");
    CHECK(fold(L"Techland Sp. z o.o.") == L"techland");
    CHECK(fold(L"Brøderbund") == L"broderbund");
    CHECK(fold(L"").empty() && fold(L"Inc.") == L"inc"); // One word is the name, even a legal one.

    // ---- The catalog: unique ids, a logo for each (in res/studios when the source tree is given).
    std::set<std::string_view> ids;
    for (const auto& studio : catalog()) {
        CHECK(!studio.id.empty() && !studio.name.empty() && studio.file.ends_with(L".png") && ids.insert(studio.id).second);
        CHECK(!studio.names.empty() || !studio.prefixes.empty());
        for (const auto name : studio.names) CHECK(fold(name) == name);
        for (const auto prefix : studio.prefixes) CHECK(fold(prefix) == prefix);
        CHECK(byId(studio.id) == &studio);
    }
    if (argc > 1) {
        const std::filesystem::path folder = std::filesystem::path(argv[1]) / L"res" / L"studios";
        for (const auto& studio : catalog()) CHECK(std::filesystem::is_regular_file(folder / std::wstring(studio.file)));
    }

    // ---- Matching.
    const auto id = [](std::wstring_view name) { const auto* studio = match(name); return studio ? studio->id : std::string_view{}; };
    CHECK(id(L"CAPCOM Co., Ltd.") == "capcom" && id(L"Capcom") == "capcom");
    CHECK(id(L"Ubisoft Montreal") == "ubisoft" && id(L"Ubisoft Entertainment") == "ubisoft" && id(L"Ubisoftware").empty());
    CHECK(id(L"BANDAI NAMCO Entertainment Inc.") == "bandai-namco" && id(L"Namco") == "namco" && id(L"NAMCO BANDAI Games") == "bandai-namco");
    CHECK(id(L"2K") == "2k" && id(L"2K Games") == "2k" && id(L"2K Sports") == "2k-sports");
    CHECK(id(L"Electronic Arts") == "electronic-arts" && id(L"EA") == "electronic-arts" && id(L"EA Sports") == "ea-sports" && id(L"EA DICE") == "electronic-arts");
    CHECK(id(L"Easy Trigger").empty() && id(L"Eastasiasoft").empty());
    CHECK(id(L"THQ") == "thq" && id(L"THQ Nordic").empty());
    CHECK(id(L"Xbox Game Studios") == "microsoft" && id(L"Microsoft Studios") == "microsoft");
    CHECK(id(L"PlayStation PC LLC") == "sony" && id(L"Sony Interactive Entertainment") == "sony");
    CHECK(id(L"Bethesda Softworks") == "bethesda" && id(L"Bethesda Game Studios") == "bethesda");
    CHECK(id(L"Rockstar North") == "rockstar" && id(L"Team17 Digital Ltd") == "team17" && id(L"Lucasfilm Games") == "lucasarts");
    CHECK(id(L"SQUARE ENIX") == "square-enix" && id(L"Square Enix Montréal") == "square-enix");
    CHECK(id(L"Valve").empty() && id(L"Devolver Digital").empty() && id(L"").empty()); // No logo: not offered.

    // ---- Grouping: once per game, most games first, then by name.
    const auto groups = group({
        {"steam:1", {L"Capcom", L"CAPCOM Co., Ltd."}},
        {"steam:2", {L"Ubisoft", L"Valve"}},
        {"steam:3", {L"Ubisoft Montreal"}},
        {"steam:4", {L"Atari"}},
        {"steam:5", {}},
        {"steam:6", {L"Capcom", L"Sega"}},
    });
    CHECK(groups.size() == 4);
    CHECK(groups[0].studio->id == "capcom" && groups[0].games == std::vector<std::string>({"steam:1", "steam:6"}));
    CHECK(groups[1].studio->id == "ubisoft" && groups[1].games.size() == 2);
    CHECK(groups[2].studio->id == "atari" && groups[3].studio->id == "sega"); // One game each: by name.

    // ---- Keys.
    CHECK(key(Role::publisher, "capcom") == "publisher:capcom" && key(Role::developer, "sega") == "developer:sega");
    const auto parsed = parse("developer:capcom");
    CHECK(parsed && parsed->first == Role::developer && parsed->second->id == "capcom");
    CHECK(!parse("publisher:nobody") && !parse("source:steam") && !parse("capcom"));
    std::cout << "Studios: folding, catalog, matching, grouping and keys passed\n";
    return 0;
}
