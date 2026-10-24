#include "../src/library/custom_games.hpp"
#include "../src/library/store_adapter.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {
custom_games::Entry entry(std::string id, std::wstring name, std::wstring path, std::wstring arguments = {}, std::wstring directory = {}) {
    return {std::move(id), std::move(name), std::move(path), std::move(arguments), std::move(directory)};
}
std::string document(const std::string& games) { return R"({"version":1,"games":[)" + games + "]}"; }
} // namespace

int main() {
    using namespace custom_games;
    i18n::setLanguage(i18n::Language::pl);

    // (1) Round trip, including JSON escapes, Unicode and an empty (default) directory.
    const std::vector<Entry> games{
        entry("0123456789abcdef", L"Wiedźmin 3 — \"GOTY\"", L"C:\\Gry\\Wiedźmin\\witcher3.exe", L"-config \"C:\\cfg\\a b.ini\" -fullscreen"),
        entry("fedcba9876543210", L"Emulator", L"D:\\Tools\\start.bat", {}, L"D:\\Tools\\data"),
        entry("00000000000000ff", L"Link", L"\\\\server\\share\\Games\\Game.lnk")};
    for (const auto& game : games) if (validate(game) != Problem::none) return 1;
    const auto text = format(games);
    const auto parsed = parse(text);
    if (!parsed.readable || parsed.skipped || parsed.games != games) return 2;
    if (!parse("\xef\xbb\xbf" + text).readable || parse("\xef\xbb\xbf" + text).games != games) return 3;
    if (format({}) != "{\n  \"version\": 1,\n  \"games\": []\n}\n" || !parse(format({})).readable || !parse(format({})).games.empty()) return 4;

    // (2) Corrupt documents never throw and yield an empty, unreadable list.
    for (const std::string bad : {std::string{}, std::string("{"), std::string("[]"), std::string("null"),
                                  std::string(R"({"version":2,"games":[]})"), std::string(R"({"version":"1","games":[]})"),
                                  std::string(R"({"version":1,"games":{}})"), std::string(R"({"games":[]})"),
                                  std::string(3 * 1024 * 1024, '['), std::string("\x00\xff\xfe", 3)}) {
        const auto result = parse(bad);
        if (result.readable || !result.games.empty()) return 5;
    }

    // (3) Invalid entries are skipped one by one; valid neighbours survive.
    const auto good = R"({"id":"aaaaaaaaaaaaaaaa","name":"Good","path":"C:\\g.exe"})";
    const std::string invalid[]{
        R"({"name":"No id","path":"C:\\g.exe"})",
        R"({"id":"AAAAAAAAAAAAAAAA","name":"Upper","path":"C:\\g.exe"})",
        R"({"id":"aaaaaaaaaaaaaaa","name":"Short id","path":"C:\\g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"   ","path":"C:\\g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":")" + std::string(201, 'x') + R"(","path":"C:\\g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Line\nbreak","path":"C:\\g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Relative","path":"games\\g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Drive relative","path":"C:g.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Folder","path":"C:\\"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Wildcard","path":"C:\\*.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Control","path":"C:\\a\tb.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Arguments","path":"C:\\g.exe","arguments":")" + std::string(2001, 'a') + R"("})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":"Directory","path":"C:\\g.exe","directory":"relative"})",
        R"({"id":"aaaaaaaaaaaaaaaa","name":"Duplicate","path":"C:\\other.exe"})",
        R"({"id":"bbbbbbbbbbbbbbbb","name":42,"path":"C:\\g.exe"})",
        R"("not an object")", R"(7)", R"([])"};
    std::string list = good;
    for (const auto& item : invalid) list += "," + item;
    const auto filtered = parse(document(list));
    if (!filtered.readable || filtered.games.size() != 1 || filtered.games[0].name != L"Good" ||
        filtered.skipped != std::size(invalid)) return 6;
    // Surrounding spaces/quotes are cleaned; arguments are optional.
    const auto cleaned = parse(document(R"({"id":"cccccccccccccccc","name":"  Spaced  ","path":" \"C:\\Games\\x.exe\" "})"));
    if (cleaned.games.size() != 1 || cleaned.games[0].name != L"Spaced" || cleaned.games[0].path != L"C:\\Games\\x.exe" ||
        !cleaned.games[0].arguments.empty() || !cleaned.games[0].directory.empty()) return 7;
    if (cleanPath(L"  \"D:\\a b\\c.lnk\"  ") != L"D:\\a b\\c.lnk" || cleanPath(L"\"\"") != L"" || trim(L" \t x \t") != L"x") return 8;
    std::string many;
    for (size_t i = 0; i < max_games + 5; ++i) {
        char id[17]{}; snprintf(id, sizeof(id), "%016zx", i);
        many += std::string(i ? "," : "") + R"({"id":")" + id + R"(","name":"G","path":"C:\\g.exe"})";
    }
    const auto capped = parse(document(many));
    if (capped.games.size() != max_games || capped.skipped != 5) return 9;

    // (4) IDs: random 64-bit hex, unique within the list, never reused after collisions.
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        const auto id = newId(games);
        if (!validId(id) || !seen.insert(id).second || id == games[0].id) return 10;
    }
    // The ID is the identity: renaming or moving keeps the key, history and artwork cache.
    auto renamed = games[0]; renamed.name = L"The Witcher 3"; renamed.path = L"E:\\Games\\witcher3.exe";
    const auto original = makeGame(games[0], true), moved = makeGame(renamed, true);
    if (gameKey(original) != "custom:0123456789abcdef" || gameKey(moved) != gameKey(original) ||
        gameCacheKey(moved) != gameCacheKey(original) || original.source != GameSource::custom || !isGame(original) ||
        std::string(sourceSlug(GameSource::custom)) != "custom" || std::wstring(sourceName(GameSource::custom)) != L"Moje gry") return 11;

    // (5) Launch action: file + arguments, working directory defaults to the file's folder.
    const auto play = primaryGameAction(original);
    if (play.target != L"C:\\Gry\\Wiedźmin\\witcher3.exe" || play.arguments != L"-config \"C:\\cfg\\a b.ini\" -fullscreen" ||
        play.working_directory != L"C:\\Gry\\Wiedźmin" || play.kind != ActionKind::play) return 12;
    if (original.executable != L"C:\\Gry\\Wiedźmin\\witcher3.exe" || original.install_directory != L"C:\\Gry\\Wiedźmin") return 13;
    const auto script = makeGame(games[1], true);
    const auto script_action = primaryGameAction(script);
    if (script_action.target != L"D:\\Tools\\start.bat" || !script_action.arguments.empty() ||
        script_action.working_directory != L"D:\\Tools\\data" || !script.executable.empty() || script.install_directory != L"D:\\Tools\\data") return 14;
    auto upper = games[0]; upper.path = L"C:\\Gry\\GAME.EXE";
    if (makeGame(upper, true).executable != L"C:\\Gry\\GAME.EXE" || !makeGame(games[2], true).executable.empty()) return 15;
    // A missing file is listed but has nothing to launch.
    const auto missing = makeGame(games[0], false);
    if (primaryGameAction(missing) || std::wstring(primaryActionLabel(missing)) != L"Brak pliku" ||
        std::wstring(primaryActionLabel(original)) != L"Uruchom") return 16;

    // (6) Files: missing = empty and readable; save/load round trip; a damaged file is kept aside.
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-custom-games-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const auto file = root / L"custom-games.json";
    const auto absent = load(file);
    if (!absent.readable || !absent.games.empty()) return 17;
    if (!save(games, file) || load(file).games != games || !load(file).readable) return 18;
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "{\"version\":1,\"games\":[{";
    const auto damaged = load(file);
    auto backup = file; backup += L".bak";
    std::ifstream kept(backup, std::ios::binary);
    const std::string kept_text(std::istreambuf_iterator<char>{kept}, {});
    if (damaged.readable || !damaged.games.empty() || kept_text != "{\"version\":1,\"games\":[{") return 19;
    std::vector<Entry> too_many(max_games + 1, games[0]);
    if (save(too_many, file)) return 20;

    std::cout << "User-added games: storage, validation, identity and launch actions passed\n";
    return 0;
}
