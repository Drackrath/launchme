// Amazon Games / Nile contracts with fixture files and a fake helper: no network, no Amazon account
// and no real Nile is ever used by this test.
#include "../src/store/amazon.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/net/json.hpp"
#include <Windows.h>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <iostream>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::cerr << "Amazon check " << __LINE__ << ": " << #x << '\n'; return __LINE__; } } while (false)

namespace {
constexpr auto product = "amzn1.adg.product.0b1d1c6e-3f1a-4a6b-9e0f-1c2d3e4f5a6b";
constexpr auto other = "amzn1.adg.product.9a8b7c6d-5e4f-4a3b-8c2d-1e0f9a8b7c6d";
std::string library(const std::string& first = product) {
    return R"([{"id":"ent-1","product":{"id":")" + first + R"(","title":"Moonlit: Archive™","sku":"SKU-1",
        "productDetail":{"iconUrl":"https://m.media-amazon.com/icon.png","details":{"backgroundUrl1":"https://m.media-amazon.com/bg.jpg","logoUrl":"https://m.media-amazon.com/logo.png"}}}},
      {"id":"ent-2","product":{"id":")" + std::string(other) + R"(","title":"Second Game","sku":"SKU-2","productDetail":{"details":{}}}},
      {"id":"ent-3","product":{"id":"not-a-product","title":"Broken"}}])";
}
bool write(const std::filesystem::path& file, std::string_view text) { return atomic_file::write(file, text); }
template<class F> bool wait(F done, int ms = 5000) {
    for (int i = 0; i < ms / 10; ++i) { if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return done();
}
}

int main() {
    using namespace amazon;
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) / (L"launchme-amazon-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root, ec);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{root};

    // Sign-in contract: only Amazon's page, and the fields `nile register` needs.
    const auto login = parseLogin("INFO [X]: noise\n{\"client_id\":\"0123456789abcdef0123\",\"code_verifier\":\"abcDEF_ghi-jkl012345\",\"serial\":\"ABCDEF0123456789\",\"url\":\"https://amazon.com/ap/signin?openid.ns=x\"}\n");
    CHECK(login && login->serial == "ABCDEF0123456789" && login->url.starts_with(L"https://amazon.com/ap/signin?"));
    CHECK(!parseLogin("{\"client_id\":\"0123456789abcdef0123\",\"code_verifier\":\"abcDEF_ghi-jkl012345\",\"serial\":\"ABCDEF0123456789\",\"url\":\"https://evil.example/ap/signin?\"}"));
    CHECK(!parseLogin("not json"));
    CHECK(authorizationCode(L"https://www.amazon.com/?openid.assoc_handle=x&openid.oa2.authorization_code=ANabcDEF123%2D_x&openid.mode=id_res") == "ANabcDEF123-_x");
    CHECK(authorizationCode(L"  ANabcDEF123456  ") == "ANabcDEF123456");
    CHECK(!authorizationCode(L"https://www.amazon.com/?openid.mode=cancel"));
    CHECK(!authorizationCode(L"short"));
    CHECK(validProductId(product) && !validProductId("amzn1.adg.product.") && !validProductId("amzn1.adg.product.<script>"));
    CHECK(folderName(L"Moonlit: Archive\u2122 ", product) == L"Moonlit Archive");
    CHECK(folderName(L"???", product) == L"0b1d1c6e-3f1a-4a6b-9e0f-1c2d3e4f5a6b");
    {
        const auto progress = progressBytes("INFO [PROGRESS]:\t = Progress: 12.50 125/1000, Running for: 00:00:01, ETA: 00:00:07");
        CHECK(progress && progress->first == 125 && progress->second == 1000);
        CHECK(!progressBytes("= Downloaded: 1.00 MiB"));
        CHECK(!progressBytes("= Progress: 1.0 9/3,"));
    }

    // fuel.json: JSON5, the executable and working folder inside the installation only.
    const auto game_dir = root / L"Games" / L"Moonlit";
    std::filesystem::create_directories(game_dir / L"bin", ec);
    CHECK(write(game_dir / L"bin" / L"Game.exe", "MZ"));
    const std::string fuel = "{\n // comment\n \"SchemaVersion\": \"2\",\n \"Main\": { \"Command\": \"bin\\\\Game.exe\", \"Args\": [\"-windowed\", \"a b\",], \"WorkingSubdirOverride\": \"bin\", },\n}\n";
    {
        const auto action = fuelAction(fuel, game_dir);
        CHECK(action && action->target == (game_dir / L"bin" / L"Game.exe").native() && action->arguments == L"\"-windowed\" \"a b\"" &&
              action->working_directory == game_dir / L"bin");
        CHECK(!fuelAction("{\"Main\":{\"Command\":\"..\\\\..\\\\cmd.exe\"}}", game_dir));
        CHECK(!fuelAction("{\"Main\":{\"Command\":\"C:\\\\Windows\\\\notepad.exe\"}}", game_dir));
        CHECK(!fuelAction("{\"Main\":{\"Command\":\"missing.exe\"}}", game_dir));
    }
    CHECK(write(game_dir / L"fuel.json", fuel));

    // The library: account games, installs startable with the SDK's variables; signed out only installs.
    const auto installed = "[{\"id\":\"" + std::string(product) + "\",\"version\":\"v1\",\"path\":" + json::quote(text_encoding::utf8(game_dir.native())) + ",\"size\":2}]";
    {
        const auto games = parseLibrary(library(), installed, "amzn1.account.TEST", L"Pat", root / L"nile");
        CHECK(games.size() == 2 && games[0].installed && !games[1].installed && games[0].account_id == "amzn1.account.TEST");
        CHECK(games[0].name == L"Moonlit: Archive\u2122" && games[0].store_tall_art == "https://m.media-amazon.com/icon.png" &&
              games[0].store_wide_art == "https://m.media-amazon.com/bg.jpg" && games[0].store_logo_art == "https://m.media-amazon.com/logo.png");
        const auto& environment = games[0].play_action.environment;
        const auto value = [&](std::wstring_view name) {
            for (const auto& [key, text] : environment) if (key == name) return text;
            return std::wstring{};
        };
        CHECK(value(L"AMAZON_GAMES_FUEL_ENTITLEMENT_ID") == L"ent-1" && value(L"AMAZON_GAMES_FUEL_PRODUCT_SKU") == L"SKU-1" &&
              value(L"AMAZON_GAMES_FUEL_DISPLAY_NAME") == L"Pat" && value(L"FUEL_DIR").ends_with(L"Amazon Games Services\\Legacy"));
        CHECK(games[0].play_action.target == (game_dir / L"bin" / L"Game.exe").native());
        const auto signed_out = parseLibrary(library(), installed, {}, {}, root / L"nile");
        CHECK(signed_out.size() == 1 && signed_out[0].installed && signed_out[0].account_id.empty());
        CHECK(parseLibrary("{}", installed, "a", {}, root).empty() && parseLibrary("[", "", "a", {}, root).empty());
    }

    // The service with a fake Nile: sign-in, sync, install with progress, uninstall and sign-out.
    const auto config = root / L"config";
    const auto nile = nileDirectory(config);
    std::vector<std::vector<std::wstring>> calls;
    std::mutex calls_mutex;
    Operations operations;
    operations.prepare = [&](const std::filesystem::path& c, bool, const Cancel&, const std::function<void(uint64_t)>&) {
        return c == config ? std::filesystem::path(L"C:\\fake\\nile.exe") : std::filesystem::path{};
    };
    operations.invoke = [&](const std::filesystem::path&, const std::filesystem::path& c, const std::vector<std::wstring>& args, const Cancel&,
                            const std::function<void(std::string_view)>& line) {
        { std::lock_guard lock(calls_mutex); calls.push_back(args); }
        helper_process::Result result; result.started = true; result.ok = c == config; result.exit_code = 0;
        if (args.size() >= 2 && args[0] == L"auth" && args[1] == L"--login")
            result.output = "{\"client_id\":\"0123456789abcdef0123\",\"code_verifier\":\"abcDEF_ghi-jkl012345\",\"serial\":\"ABCDEF0123456789\",\"url\":\"https://amazon.com/ap/signin?x=1\"}";
        else if (!args.empty() && args[0] == L"register") {
            const bool expected = args.size() == 9 && args[2] == L"ANabcDEF123456" && args[4] == L"0123456789abcdef0123" && args[8] == L"ABCDEF0123456789";
            if (expected) {
                write(nile / L"current_user.json", "{\"name\":\"Pat\",\"user_id\":\"amzn1.account.TEST\"}");
                write(nile / L"user.enc", "x");
                write(nile / L"library.json", library());
                write(nile / L"syncpoint.raw", "1790000000.5");
            }
        } else if (!args.empty() && args[0] == L"install") {
            if (line) { line("INFO [PROGRESS]:\t = Progress: 50.00 1/2, Running for: 00:00:01"); line("INFO [PROGRESS]:\t = Progress: 100.00 2/2, x"); }
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // The poller sees the progress.
            const std::filesystem::path target(args[3]);
            std::error_code e; std::filesystem::create_directories(target / L"bin", e);
            write(target / L"bin" / L"Game.exe", "MZ"); write(target / L"fuel.json", fuel);
            write(nile / L"installed.json", "[{\"id\":\"" + std::string(product) + "\",\"version\":\"v1\",\"path\":" + json::quote(text_encoding::utf8(target.native())) + "}]");
        } else if (!args.empty() && args[0] == L"uninstall") write(nile / L"installed.json", "[]");
        return result;
    };
    {
        auto service = makeService(config, operations);
        std::optional<Snapshot> snapshot;
        const auto next = [&] { return wait([&] { if (auto s = service->take()) { snapshot = std::move(s); return true; } return false; }); };
        service->submit(Command::restore);
        CHECK(next() && snapshot->state == State::disconnected && snapshot->account.empty());
        service->submit(Command::connect);
        CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->state == State::signing_in; }));
        CHECK(snapshot->browser_url == L"https://amazon.com/ap/signin?x=1");
        service->submit(Command::complete, L"https://www.amazon.com/?openid.oa2.authorization_code=bad");
        CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->failure == Failure::invalid; }));
        CHECK(snapshot->state == State::signing_in);
        service->submit(Command::complete, L"https://www.amazon.com/?openid.oa2.authorization_code=ANabcDEF123456&x=y");
        CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->state == State::connected; }));
        CHECK(snapshot->account == "amzn1.account.TEST" && snapshot->name == L"Pat" && snapshot->games == 2 && snapshot->synced_at == 1790000000);
        service->submit(Command::sync);
        CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->state == State::connected && !service->busy(); }));
        {
            std::lock_guard lock(calls_mutex);
            CHECK(std::any_of(calls.begin(), calls.end(), [](const auto& a) { return a.size() == 2 && a[0] == L"library" && a[1] == L"sync"; }));
        }
        // Install: only an owned game, into the chosen folder, progress in bytes, verified afterwards.
        Game game; game.source = GameSource::amazon; game.kind = GameKind::store; game.source_id = product; game.name = L"Moonlit";
        gog::InstallRequest request; request.product = product; request.install_directory = root / L"Games" / L"Amazon-Moonlit";
        CHECK(service->install(game, request));
        CHECK(!service->install(game, request)); // One at a time.
        gog::InstallSnapshot install; bool saw_bytes = false;
        CHECK(wait([&] {
            while (auto s = service->takeInstall()) { install = *s; saw_bytes |= install.state == gog::InstallState::downloading && install.total == 2; }
            return install.state == gog::InstallState::completed || install.state == gog::InstallState::error;
        }));
        if (install.state != gog::InstallState::completed) std::cerr << "install state " << static_cast<int>(install.state) << " error " << static_cast<int>(install.error) << "\n";
        CHECK(install.state == gog::InstallState::completed && saw_bytes);
        const auto profile = readProfile(config);
        CHECK(profile.account && profile.games.size() == 2 && profile.games[0].installed &&
              profile.games[0].install_directory == request.install_directory);
        Game stranger = game; stranger.source_id = "amzn1.adg.product.11111111-2222-3333-4444-555555555555";
        CHECK(service->install(stranger, request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::error; }));
        CHECK(install.error == gog::InstallError::account);
        CHECK(uninstall(config, profile.games[0], [] { return false; }, operations));
        CHECK(!readProfile(config).games[0].installed);
        service->submit(Command::logout);
        CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->account.empty() && !service->busy(); }));
        CHECK(!std::filesystem::exists(nile / L"current_user.json") && !std::filesystem::exists(nile / L"user.enc"));
    }
    // Status texts for every state exist (no empty status in Ustawienia).
    for (const auto state : {State::disconnected, State::preparing, State::signing_in, State::verifying, State::syncing, State::connected, State::error}) {
        Snapshot s; s.state = state; CHECK(!statusText(s).empty());
    }
    std::cout << "Amazon / Nile contracts passed\n";
    return 0;
}
