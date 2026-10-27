// Protocol and filesystem contracts, using this executable as the fake child. No Steam session,
// network or real Aurelia executable is ever used by this test.
#include "../src/store/aurelia.hpp"
#include "../src/store/aurelia_stream.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/updater.hpp"
#include <Windows.h>
#include <iostream>
#include <thread>
#include <atomic>

#define CHECK(x) do { if (!(x)) { std::cerr << "Aurelia check " << __LINE__ << ": " << #x << '\n'; return __LINE__; } } while (false)
using C = aurelia::Command;
json::Value parsed(std::string_view text) { const auto v = json::parse(text); if (!v) throw std::runtime_error("fixture"); return *v; }

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--json") {
        const std::string command = argc > 2 ? argv[2] : "";
        if (command == "login") {
            wchar_t profile[4096]{}; if (!GetEnvironmentVariableW(L"AURELIA_CONFIG_DIR", profile, 4096)) return 2;
            wchar_t endpoint[4096]{};
            if (!GetEnvironmentVariableW(L"AURELIA_DAEMON_SOCKET", endpoint, 4096) || aurelia::daemonEndpoint(profile) != endpoint) return 3;
            std::cout << R"({"logged_in":true,"steam_id":76561198000000001,"account":"fixture"})"; return 0;
        }
        if (command == "available") { std::cerr << "log before result\n\x1b[32m{\n  \"available\": true\n}\x1b[0m\n"; return 0; }
        if (command == "info" && argc > 3 && std::string_view(argv[3]) == "903") { for (int i = 0; i < 18000; ++i) std::cout << std::string(1024, 'x'); return 0; }
        if (command == "info") { Sleep(15000); return 0; }
        if (command == "chat") {
            std::cout << R"({"event":"ready","with":76561198000000001})" << std::endl;
            while (true) Sleep(50);
        }
        if (command == "verify") { std::cerr << R"({"event":"progress","state":"verifying","bytes_downloaded":1,"total_bytes":4})" << std::endl;
            std::cout << R"({"app_id":902,"status":"verified"})"; return 0; }
        std::cout << R"({"error":"fixture failure"})"; return 7;
    }
    i18n::setLanguage(i18n::Language::en);
    const auto big = parsed("18446744073709551615"), precision = parsed("8593343465227540543");
    CHECK(aurelia::integer(&big) == UINT64_MAX && aurelia::integer(&precision) == 8593343465227540543ULL);
    for (const auto invalid : {"-1", "1.0", "1e3", "18446744073709551616", "null", "true"}) { const auto n = parsed(invalid); CHECK(!aurelia::integer(&n)); }
    aurelia::Request request{C::downgrade, 902, 903, 8593343465227540543ULL};
    CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"downgrade", L"902", L"--depot", L"903", L"--manifest", L"8593343465227540543"}));
    request = {C::chat_send}; request.id = 76561198000000001ULL; request.text = L"--help & \"hello\" \\";
    CHECK(aurelia::arguments(request).back() == request.text && aurelia::arguments(request)[4] == L"--");
    request.text.assign(4001, L'a'); CHECK(aurelia::arguments(request).empty());
    request = {C::play, 902}; CHECK(aurelia::arguments(request).back() == L"--steam");
    request.steam = false; CHECK(aurelia::arguments(request)[6] == L"none" && aurelia::arguments(request)[5] == L"--steam-mode");
    request = {C::presence}; request.text = L"bogus"; CHECK(aurelia::arguments(request).empty());
    request = {C::move, 902}; request.folder = L"relative"; CHECK(aurelia::arguments(request).empty());
    CHECK(aurelia::arguments({C::uninstall, 0}).empty());
    request = {C::cloud_sync, 902}; request.direction = aurelia::Request::Direction::down;
    CHECK(aurelia::arguments(request).back() == L"--down");
    std::vector<json::Value> frames;
    aurelia::JsonStream stream;
    const std::string chunks = "log [debug]\n\x1b[32m{\n\"id\":8593343465227540543,\"text\":\"braces {} [ ] and \\\"quote\\\"\"}\x1b[0m\n[1,2]";
    for (char c : chunks) CHECK(stream.feed(std::string_view(&c, 1), [&](const auto& item) { frames.push_back(item); }));
    CHECK(frames.size() == 2 && aurelia::integer(frames[0].get("id")) == 8593343465227540543ULL && !stream.incomplete());
    aurelia::JsonStream small(8); CHECK(!small.feed("{\"123456789\":0}", {}));
    aurelia::JsonStream incomplete; CHECK(incomplete.feed("{\"id\":", {}) && incomplete.incomplete());
    const auto games = aurelia::library(parsed(R"([{"app_id":902,"name":"Fixture","is_installed":true,"is_owned":true,"install_path":"D:\\Library\\Fixture","playtime_forever_minutes":120}])"), 76561198000000001ULL);
    CHECK(games && games->size() == 1 && games->front().store_playtime_minutes == 120 && games->front().account_id == "76561198000000001");
    CHECK(!aurelia::library(parsed(R"([{"app_id":902,"name":"Broken"}])"), 1));
    CHECK(!aurelia::library(parsed("{}"), 1));
    CHECK(!aurelia::library(parsed("[]"), 0));
    const auto friend_list = aurelia::friends(parsed(R"([{"steam_id":76561198000000001,"persona_name":"Friend","persona_state":1,"game_name":"Fixture"}])"));
    CHECK(friend_list.friends.size() == 1 && friend_list.friends[0].id == 76561198000000001ULL && friend_list.friends[0].status == 1);
    auto cloud = aurelia::cloud({true, false, parsed(R"({"files":[{"filename":"save.dat"}]})")}, false);
    CHECK(cloud.state == aurelia::CloudState::files); // A remote file listing never claims synchronization.
    cloud = aurelia::cloud({true, false, parsed(R"({"status":"ok","failed":[{"filename":"save.dat"}]})")}, true);
    CHECK(cloud.state == aurelia::CloudState::failed); // Exit 0 is not a successful restore.
    cloud = aurelia::cloud({true, false, parsed(R"({"status":"conflicts","conflicts":[{}]})")}, true);
    CHECK(cloud.state == aurelia::CloudState::conflicts && cloud.conflicts == 1);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"incomplete","skipped":[{}]})")}, true).state == aurelia::CloudState::incomplete);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"ok","conflicts":[],"failed":[],"skipped":[]})")}, true).state == aurelia::CloudState::synced);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"ok","conflicts":"broken"})")}, true).state == aurelia::CloudState::failed);
    CHECK(!aurelia::checked({C::install, 902}, {true, false, parsed(R"({"app_id":902,"status":"cancelled"})")}).ok);
    CHECK(!aurelia::checked({C::verify, 902}, {true, false, parsed(R"({"app_id":903,"status":"verified"})")}).ok);
    const auto dlc = aurelia::dlcs(parsed(R"({"dlc":[{"app_id":903,"name":"DLC","owned":null,"installed":false,"disabled":false}]})"));
    CHECK(dlc && !dlc->front().owned.has_value());
    const auto progress = aurelia::downloads(parsed(R"([{"app_id":902,"downloaded_bytes":0,"total_bytes":9000000000,"is_downloading":true}])"));
    CHECK(progress && progress->front().progress.percent() == 0 && progress->front().progress.total == 9000000000ULL);
    CHECK(!aurelia::downloads(parsed("{}")));
    const auto history = aurelia::chatHistory(parsed(R"([{"timestamp":20,"from_self":true,"message":"second"},{"timestamp":10,"from_self":false,"message":"first"}])"));
    CHECK(history && history->front().text == L"first");
    CHECK(!aurelia::chatHistory(parsed(R"([{"timestamp":20,"message":"bad"}])")));
    const auto achievements = aurelia::achievements(parsed(R"({"achievements":[{"achievement_key":"FIRST","name":"First","visible":true,"unlocked":true,"unlock_time":123,"rarity":3.4}]})"));
    CHECK(achievements.complete && achievements.total == 1 && achievements.unlocked == 1 && achievements.items[0].unlocked_at == 123);

    const std::string hash(64, 'a');
    const auto release_text = R"({"tag_name":"v0.1.40","assets":[{"name":"aurelia_windows_x86_64.exe","browser_download_url":"https://github.com/Drackrath/Aurelia/releases/download/v0.1.40/aurelia_windows_x86_64.exe","size":8693760,"digest":"sha256:)" + hash + R"("}]})";
    const auto release = aurelia::parseRelease(release_text);
    CHECK(release && release->size == 8693760 && release->sha256 == hash);
    CHECK(!aurelia::parseRelease("{}") && !aurelia::parseRelease("broken"));
    auto wrong = release_text; wrong.replace(wrong.find("windows_x86_64"), 14, "windows_arm64"); CHECK(!aurelia::parseRelease(wrong));
    CHECK(aurelia::allowedDownload(release->url) && !aurelia::allowedDownload(L"https://github.com.evil/Drackrath/Aurelia/releases/download/a"));
    CHECK(!aurelia::allowedDownload(L"https://github.com/other/Aurelia/releases/download/a"));
    CHECK(!aurelia::allowedDownload(L"http://release-assets.githubusercontent.com/a"));

    const auto root = std::filesystem::temp_directory_path() / (L"launchme-aurelia-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code e; std::filesystem::remove_all(root, e); } } cleanup{root};
    const auto roster = parsed(R"([{"app_id":902,"name":"Owned","is_installed":true,"is_owned":true}])");
    CHECK(aurelia::saveLibraryCache(root / L"cache", 76561198000000001ULL, roster));
    CHECK(aurelia::loadLibraryCache(root / L"cache", 76561198000000001ULL));
    CHECK(!aurelia::loadLibraryCache(root / L"cache", 76561198000000002ULL));
    CHECK(!aurelia::saveLibraryCache(root / L"cache", 0, roster));
    std::vector<C> calls; bool shared = false, target = true, conflict = false, upload_failure = false;
    aurelia::Transport workflow = [&](const aurelia::Request& r, const aurelia::Cancel&, const aurelia::Event&) {
        calls.push_back(r.command);
        auto reply = [&](std::string_view value) { return aurelia::Reply{true, false, parsed(value)}; };
        if (r.command == C::library) return shared ? reply(R"([{"app_id":902,"name":"Borrowed","is_owned":false,"is_family_shared":true}])") : aurelia::Reply{true, false, roster};
        if (r.command == C::launch_targets) return target ? reply(R"({"targets":[{"platform":"windows","steam_mode":"none","available":true}]})") : reply(R"({"targets":[{"platform":"windows","steam_mode":"host","available":true}]})");
        if (r.command == C::play) return reply(R"({"app_id":902,"status":"finished"})");
        if (r.command == C::cloud_sync) {
            if (conflict && r.direction == aurelia::Request::Direction::down) return reply(R"({"status":"conflicts","conflicts":[{"filename":"save.dat"}]})");
            if (upload_failure && r.direction == aurelia::Request::Direction::up) return reply(R"({"status":"failed","failed":[{"filename":"save.dat"}]})");
            return reply(R"({"status":"ok","failed":[],"skipped":[],"conflicts":[]})");
        }
        return aurelia::Reply{};
    };
    request = {C::standalone, 902}; request.auto_cloud = true;
    CHECK(aurelia::launchStandalone(workflow, request, [] { return false; }).ok);
    CHECK(calls == std::vector<C>({C::library, C::launch_targets, C::cloud_sync, C::play, C::cloud_sync}));
    shared = true; calls.clear(); CHECK(!aurelia::launchStandalone(workflow, request, [] { return false; }).ok && calls.size() == 1);
    shared = false; target = false; calls.clear(); CHECK(!aurelia::launchStandalone(workflow, request, [] { return false; }).ok && calls.size() == 2);
    target = true; conflict = true; calls.clear();
    auto blocked = aurelia::launchStandalone(workflow, request, [] { return false; });
    CHECK(!blocked.ok && !aurelia::flag(blocked.data, "launch_finished") && calls.size() == 3);
    conflict = false; upload_failure = true; calls.clear();
    auto failed_upload = aurelia::launchStandalone(workflow, request, [] { return false; });
    CHECK(!failed_upload.ok && aurelia::flag(failed_upload.data, "launch_finished") && calls.size() == 5);
    // Minimal PE header: validates architecture, never executed.
    std::string image(4096, '\0'); IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 128;
    memcpy(image.data(), &dos, sizeof(dos)); const DWORD sig = IMAGE_NT_SIGNATURE; memcpy(image.data() + 128, &sig, 4);
    IMAGE_FILE_HEADER header{}; header.Machine = IMAGE_FILE_MACHINE_AMD64; memcpy(image.data() + 132, &header, sizeof(header));
    const auto partial = root / L"fixture.part"; CHECK(atomic_file::write(partial, image));
    aurelia::Release package{L"v1.0.0", {}, image.size(), updater::sha256File(partial)};
    CHECK(aurelia::publish(root / L"helper", package, partial));
    CHECK(aurelia::installed(root / L"helper").version == L"v1.0.0");
    CHECK(atomic_file::write(partial, "bad executable")); package.version = L"v1.0.1";
    CHECK(!aurelia::publish(root / L"helper", package, partial));
    CHECK(aurelia::installed(root / L"helper").version == L"v1.0.0"); // Failed update retains the previous install.
    CHECK(atomic_file::write(root / L"helper" / L"current.txt", "../escape")); CHECK(aurelia::installed(root / L"helper").executable.empty());

    wchar_t self[4096]{}; CHECK(GetModuleFileNameW(nullptr, self, 4096));
    aurelia::Runtime runtime{self, root / L"profile"};
    const auto pipe = aurelia::daemonEndpoint(runtime.profile);
    CHECK(pipe.starts_with(L"\\\\.\\pipe\\launchme-aurelia-") && pipe != aurelia::daemonEndpoint(root / L"other-profile"));
    CHECK(pipe == aurelia::daemonEndpoint(root / L"." / L"profile"));
    // An existing endpoint without its identity marker must never invoke the CLI's broad stale-daemon fallback.
    const HANDLE server = CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 1, 4096, 4096, 0, nullptr);
    CHECK(server != INVALID_HANDLE_VALUE);
    OVERLAPPED pending{}; pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(server, &pending);
    auto unidentified = aurelia::invoke(runtime, {C::health}, [] { return false; });
    CHECK(!unidentified.ok);
    CancelIoEx(server, &pending); WaitForSingleObject(pending.hEvent, 1000); CloseHandle(server); CloseHandle(pending.hEvent);
    std::cout << "Aurelia native runner: query" << std::endl;
    const auto never = [] { return false; };
    auto reply = aurelia::invoke(runtime, {C::health}, never);
    CHECK(reply.ok && aurelia::integer(reply.data.get("steam_id")) == 76561198000000001ULL);
    reply = aurelia::invoke(runtime, {C::available, 902}, never); CHECK(reply.ok && aurelia::flag(reply.data, "available"));
    bool event = false; reply = aurelia::invoke(runtime, {C::verify, 902}, never, [&](const auto& v) { if (aurelia::text(v, "event") == "progress") event = true; });
    CHECK(reply.ok && event);
    reply = aurelia::invoke(runtime, {C::unpin, 902}, never); CHECK(!reply.ok && reply.error == L"fixture failure");
    const auto start = GetTickCount64(); reply = aurelia::invoke(runtime, {C::info, 902}, [start] { return GetTickCount64() - start > 200; });
    CHECK(reply.cancelled && GetTickCount64() - start < 5000);
    runtime.query_timeout_ms = 80;
    reply = aurelia::invoke(runtime, {C::info, 902}, never); CHECK(!reply.ok && !reply.cancelled);
    runtime.query_timeout_ms = 90000;
    std::cout << "Aurelia native runner: output limit" << std::endl;
    reply = aurelia::invoke(runtime, {C::info, 903}, never); CHECK(!reply.ok);
    std::cout << "Aurelia native runner: streaming cancellation" << std::endl;
    std::atomic_bool stop = false; request = {C::chat_live}; request.id = 76561198000000001ULL;
    const auto live_start = GetTickCount64();
    reply = aurelia::invoke(runtime, request, [&] { return stop.load() || GetTickCount64() - live_start > 3000; }, [&](const auto& v) { if (aurelia::text(v, "event") == "ready") stop = true; });
    CHECK(stop && reply.cancelled);
    std::cout << "Aurelia protocol, installer, process cancellation and state contracts passed\n";
    return 0;
}
