// Xbox account / launchme-xbox contracts with fixture files and a fake helper: no network, no
// Microsoft account and no real helper is ever used by this test.
#include "../src/store/xbox_account.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/net/json.hpp"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::cerr << "Xbox check " << __LINE__ << ": " << #x << '\n'; return __LINE__; } } while (false)

namespace {
constexpr auto owned = "9NBLGGH4R315";
constexpr auto pass = "9PGAMEPASS01";
constexpr auto family = "Studio.Moonlit_8wekyb3d8bbwe";
std::string library() {
    return R"json({"version":1,"synced_at":1790000000,"username":"pat@example.com","game_pass":"CFQ7TTC0KGQ8","games":[
      {"product_id":"9NBLGGH4R315","title":"Moonlit: Archive","pfn":"Studio.Moonlit_8wekyb3d8bbwe","own_install":"supported","acquisition":"purchased",
       "images":{"poster":"https://store-images.s-microsoft.com/poster.png","hero":"https://store-images.s-microsoft.com/hero.jpg","logo":"https://store-images.s-microsoft.com/logo.png"}},
      {"product_id":"9PGAMEPASS01","title":"Pass Quest","pfn":null,"own_install":"unsupported","acquisition":"gamepass","images":{"box":"https://store-images.s-microsoft.com/box.png","hero":"javascript:alert(1)"}},
      {"product_id":"<bad>","title":"Broken"},
      {"product_id":"9NBLGGH4R315","title":"Duplicate","pfn":"Studio.Moonlit_8wekyb3d8bbwe"}]})json";
}
bool write(const std::filesystem::path& file, std::string_view text) { return atomic_file::write(file, text); }
std::string marker(const std::string& product, const std::string& exe) {
    return "{\"product_id\":\"" + product + "\",\"executable\":" + json::quote(exe) + ",\"display_name\":\"Moonlit\"}";
}
template<class F> bool wait(F done, int ms = 5000) {
    for (int i = 0; i < ms / 10; ++i) { if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return done();
}
std::wstring option(const std::vector<std::wstring>& args, std::wstring_view name) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return {};
}
}

int main() {
    using namespace xbox_account;
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) / (L"launchme-xbox-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root, ec);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{root};

    // Identifiers and the helper's answer: the last JSON line wins, unknown error codes are internal.
    CHECK(validProductId(owned) && !validProductId("9N") && !validProductId("9NBLG-GH4R315"));
    CHECK(validFamilyName(family) && !validFamilyName("NoHash") && !validFamilyName("Bad Name_x") && !validFamilyName("_x"));
    CHECK(folderName(L"Moonlit: Archive\u2122 ", owned) == L"Moonlit Archive" && folderName(L"???", owned) == L"9NBLGGH4R315");
    CHECK(market().size() == 2 && !language().empty());
    {
        const auto ok = parseResult("log line\n{\"ok\":true,\"username\":\"pat@example.com\",\"puid\":\"0123ABCD\"}\r\n");
        CHECK(ok && ok->ok && ok->username == "pat@example.com" && ok->puid == "0123ABCD");
        const auto failed = parseResult("{\"ok\":false,\"error\":\"not_entitled\",\"message\":\"no license\"}");
        CHECK(failed && !failed->ok && failed->error == "not_entitled" && failed->message == L"no license");
        const auto weird = parseResult("{\"ok\":false,\"error\":\"rm -rf /\"}");
        CHECK(weird && weird->error == "internal");
        CHECK(!parseResult("") && !parseResult("not json") && !parseResult("{\"ok\":tru"));
        const auto progress = parseProgress(R"({"progress":{"stage":"download","done":125,"total":1000}})");
        CHECK(progress && progress->stage == "download" && progress->done == 125 && progress->total == 1000);
        CHECK(!parseProgress(R"({"progress":{"stage":"download","done":9,"total":3}})") && !parseProgress("Downloading 5%") &&
              !parseProgress(R"({"progress":{"stage":"download","done":-1,"total":3}})"));
        CHECK(parseAccount(R"({"username":"pat@example.com","puid":"0123ABCD"})") && !parseAccount(R"({"username":"","puid":"0123ABCD"})") &&
              !parseAccount(R"({"username":"pat","puid":"<x>"})"));
    }

    // Install records: the marker names an executable inside the folder; nothing outside it.
    const auto config = root / L"config";
    const auto game_dir = root / L"Games" / L"Xbox-Moonlit";
    std::filesystem::create_directories(game_dir / L"Bin", ec);
    CHECK(write(game_dir / L"Bin" / L"Game.exe", "MZ"));
    CHECK(markerExecutable(marker(owned, "Bin\\Game.exe"), game_dir, owned) == game_dir / L"Bin" / L"Game.exe");
    CHECK(markerExecutable(marker(owned, "..\\..\\cmd.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(owned, "C:\\Windows\\notepad.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(owned, "Bin\\Missing.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(pass, "Bin\\Game.exe"), game_dir, owned).empty()); // Another product's folder.
    {
        const std::vector<OwnInstall> installs{{owned, family, game_dir}, {"<x>", family, game_dir}, {pass, pass, L"relative\\dir"}};
        const auto parsed = parseInstalls(serializeInstalls(installs));
        CHECK(parsed.size() == 1 && parsed[0].product == owned && parsed[0].directory == game_dir);
    }

    // The library: account games by package family name (else product ID), art only over https.
    {
        const auto games = parseLibrary(library(), {}, "0123ABCD", config);
        CHECK(games.size() == 2);
        CHECK(games[0].source == GameSource::xbox && games[0].source_id == family && games[0].xbox_product == owned && games[0].xbox_own_install &&
              !games[0].installed && games[0].account_id == "0123ABCD" && games[0].name == L"Moonlit: Archive");
        CHECK(games[0].store_tall_art == "https://store-images.s-microsoft.com/poster.png" && games[0].store_wide_art == "https://store-images.s-microsoft.com/hero.jpg" &&
              games[0].store_logo_art == "https://store-images.s-microsoft.com/logo.png");
        CHECK(games[1].source_id == pass && !games[1].xbox_own_install && games[1].store_tall_art == "https://store-images.s-microsoft.com/box.png" &&
              games[1].store_wide_art.empty());
        CHECK(games[0].install_action.target == L"ms-windows-store://pdp/?ProductId=9NBLGGH4R315");
        CHECK(parseLibrary(library(), {}, {}, config).empty()); // Signed out: no account games.
        CHECK(parseLibrary("[", {}, "a", config).empty() && parseLibrary("{}", {}, "a", config).empty());
        CHECK(xboxAppInstall(owned, true).target == L"msxbox://game/?productId=9NBLGGH4R315" && !xboxAppInstall("<x>", true));
    }
    // Graj of a game the helper installed runs the helper, not the (encrypted) game EXE.
    {
        const auto action = runAction(config, game_dir);
        CHECK(action.target == helperPath(config).native() && action.kind == ActionKind::play && action.working_directory == game_dir);
        CHECK(action.arguments.find(L"run") != std::wstring::npos && action.arguments.find(L"\"" + game_dir.native() + L"\"") != std::wstring::npos &&
              action.arguments.find(L"--profile") != std::wstring::npos);
    }

    // Merge: an Xbox app installation of the same package keeps its entry and gains the account's art;
    // the others install through the Xbox app when it is there.
    {
        StoreLibrary native;
        native.clients.push_back({GameSource::xbox, L"C:\\Program Files\\WindowsApps\\Microsoft.GamingApp\\XboxPcApp.exe"});
        Game local; local.kind = GameKind::store; local.source = GameSource::xbox; local.source_id = family; local.name = L"Moonlit (local)";
        local.installed = true; local.play_action = {L"shell:AppsFolder\\Studio.Moonlit_8wekyb3d8bbwe!Game"};
        native.visible.push_back(local);
        const auto merged = mergeXboxLibrary(native, parseLibrary(library(), {}, "0123ABCD", config));
        CHECK(merged.visible.size() == 2);
        CHECK(merged.visible[0].installed && merged.visible[0].name == L"Moonlit (local)" && merged.visible[0].account_id == "0123ABCD" &&
              merged.visible[0].xbox_product == owned && merged.visible[0].store_tall_art == "https://store-images.s-microsoft.com/poster.png" &&
              merged.visible[0].play_action.target.starts_with(L"shell:AppsFolder\\"));
        CHECK(merged.visible[1].install_action.target == L"msxbox://game/?productId=9PGAMEPASS01" && merged.visible[1].install_action.kind == ActionKind::install);
        native.clients.clear(); native.visible.clear();
        const auto without_app = mergeXboxLibrary(native, parseLibrary(library(), {}, "0123ABCD", config));
        CHECK(without_app.visible.size() == 2 && without_app.visible[1].install_action.target == L"ms-windows-store://pdp/?ProductId=9PGAMEPASS01");
    }

    // The service with a fake helper: sign-in, sync, install with progress, uninstall and sign-out.
    std::vector<std::vector<std::wstring>> calls;
    std::mutex calls_mutex;
    bool expire_next_sync = false;
    Operations operations;
    operations.prepare = [&](const std::filesystem::path& c, bool, const Cancel&, const std::function<void(uint64_t)>&) {
        return c == config ? std::filesystem::path(L"C:\\fake\\launchme-xbox.exe") : std::filesystem::path{};
    };
    operations.invoke = [&](const std::filesystem::path&, const std::filesystem::path& c, const std::vector<std::wstring>& args, const Cancel&,
                            const std::function<void(std::string_view)>& line) {
        { std::lock_guard lock(calls_mutex); calls.push_back(args); }
        helper_process::Result result; result.started = c == config; result.exit_code = 0; result.ok = true;
        const auto command = args.empty() ? std::wstring{} : args[0];
        if (command == L"login") result.output = "{\"ok\":true,\"signed_in\":true,\"username\":\"pat@example.com\",\"puid\":\"0123ABCD\"}";
        else if (command == L"library") {
            if (expire_next_sync) { result.ok = false; result.exit_code = 1; result.output = "{\"ok\":false,\"error\":\"signed_out\",\"message\":\"expired\"}"; }
            else { write(option(args, L"--out"), library()); result.output = "{\"ok\":true,\"games\":2}"; }
        } else if (command == L"install") {
            const std::filesystem::path target(option(args, L"--dest"));
            if (option(args, L"--product") != text_encoding::wide(owned)) {
                result.ok = false; result.output = "{\"ok\":false,\"error\":\"not_entitled\",\"message\":\"x\"}"; return result;
            }
            if (line) { line(R"({"progress":{"stage":"metadata","done":0,"total":0}})"); line(R"({"progress":{"stage":"download","done":1,"total":2}})"); }
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // The poller sees the progress.
            std::error_code e; std::filesystem::create_directories(target / L"Bin", e);
            write(target / L"Bin" / L"Game.exe", "encrypted");
            write(target / L".xodus-streaming.msixvc", "header");
            write(target / L".launchme-xbox.json", marker(owned, "Bin\\Game.exe"));
            result.output = "{\"ok\":true,\"executable\":\"Bin\\\\Game.exe\"}";
        } else if (command == L"logout") result.output = "{\"ok\":true,\"signed_in\":false}";
        else { result.ok = false; result.output = "{\"ok\":false,\"error\":\"usage\"}"; }
        return result;
    };
    {
        auto service = makeService(config, operations);
        std::optional<Snapshot> snapshot;
        const auto until = [&](auto done) { return wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && done(*snapshot); }); };
        service->submit(Command::restore);
        CHECK(until([](const Snapshot& s) { return s.state == State::disconnected; }) && snapshot->account.empty());
        service->submit(Command::connect);
        CHECK(until([](const Snapshot& s) { return s.state == State::connected; }));
        CHECK(snapshot->account == "0123ABCD" && snapshot->name == L"pat@example.com" && snapshot->games == 2 && snapshot->game_pass &&
              snapshot->synced_at == 1790000000);
        {
            std::lock_guard lock(calls_mutex);
            const auto sync = std::find_if(calls.begin(), calls.end(), [](const auto& a) { return !a.empty() && a[0] == L"library"; });
            CHECK(sync != calls.end() && option(*sync, L"--out") == libraryPath(config).native() && option(*sync, L"--market").size() == 2);
        }
        // Install: only an account game the helper supports, into the chosen folder, progress in bytes, verified.
        auto games = readProfile(config).games;
        CHECK(games.size() == 2);
        const auto moonlit = games[0];
        gog::InstallRequest request; request.product = moonlit.source_id; request.install_directory = game_dir.parent_path() / L"Xbox-Moonlit-2";
        CHECK(service->install(moonlit, request));
        CHECK(!service->install(moonlit, request)); // One at a time.
        gog::InstallSnapshot install; bool saw_bytes = false;
        CHECK(wait([&] {
            while (auto s = service->takeInstall()) { install = *s; saw_bytes |= install.state == gog::InstallState::downloading && install.total == 2; }
            return install.state == gog::InstallState::completed || install.state == gog::InstallState::error;
        }));
        if (install.state != gog::InstallState::completed) std::cerr << "install state " << static_cast<int>(install.state) << " error " << static_cast<int>(install.error) << "\n";
        CHECK(install.state == gog::InstallState::completed && saw_bytes && install.product == family);
        {
            const auto profile = readProfile(config);
            CHECK(profile.account && profile.games.size() == 2 && profile.games[0].installed && profile.games[0].install_directory == request.install_directory);
            CHECK(profile.games[0].executable == request.install_directory / L"Bin" / L"Game.exe" &&
                  profile.games[0].play_action.target == helperPath(config).native());
            Game copy = profile.games[0];
            CHECK(ownInstall(copy));
        }
        // The Game Pass title is MSIXVC2 here: the helper is never asked.
        CHECK(service->install(games[1], request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::error; }));
        CHECK(install.error == gog::InstallError::unavailable);
        Game stranger = moonlit; stranger.xbox_product = "9ZZZZZZZZZZZ";
        CHECK(service->install(stranger, request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::error; }));
        CHECK(install.error == gog::InstallError::account);

        // Uninstall: only a recorded folder with its marker; the record goes with it.
        Game unrecorded = readProfile(config).games[0]; unrecorded.install_directory = game_dir;
        CHECK(!uninstall(config, unrecorded) && std::filesystem::exists(game_dir / L"Bin" / L"Game.exe"));
        CHECK(uninstall(config, readProfile(config).games[0]));
        CHECK(!std::filesystem::exists(request.install_directory) && !readProfile(config).games[0].installed);

        // An expired session signs the account out locally.
        expire_next_sync = true;
        service->submit(Command::sync);
        CHECK(until([](const Snapshot& s) { return s.state == State::error && s.failure == Failure::expired; }));
        CHECK(!readProfile(config).account && !std::filesystem::exists(libraryPath(config)));
        expire_next_sync = false;
        service->submit(Command::connect);
        CHECK(until([](const Snapshot& s) { return s.state == State::connected; }));
        service->submit(Command::logout);
        CHECK(until([&](const Snapshot& s) { return s.account.empty() && !service->busy(); }));
        CHECK(!std::filesystem::exists(profileDirectory(config) / L"account.json") && !std::filesystem::exists(libraryPath(config)));
        std::lock_guard lock(calls_mutex);
        CHECK(std::any_of(calls.begin(), calls.end(), [](const auto& a) { return a.size() == 1 && a[0] == L"logout"; }));
    }
    // Status texts for every state exist (no empty status in Ustawienia).
    for (const auto state : {State::disconnected, State::preparing, State::signing_in, State::syncing, State::connected, State::error}) {
        Snapshot s; s.state = state; CHECK(!statusText(s).empty());
        s.failure = Failure::expired; CHECK(!statusText(s).empty());
    }
    std::cout << "Xbox account contracts passed\n";
    return 0;
}
