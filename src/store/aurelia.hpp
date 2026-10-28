#pragma once
// Aurelia is an optional out-of-process Steam integration. Only its documented CLI is used;
// credentials and session files belong to Aurelia, in Launchme's isolated profile.
#include "../net/json.hpp"
#include "../model/game.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/steam_achievements.hpp"
#include "../model/game_details.hpp"
#include <functional>
#include <optional>

namespace aurelia {
inline constexpr wchar_t latest_url[] = L"https://api.github.com/repos/Drackrath/Aurelia/releases/latest";
// The release asset for this system and the name it is installed under (<root>/<release>/<name>).
#ifdef _WIN32
inline constexpr wchar_t asset_name[] = L"aurelia_windows_x86_64.exe";
inline constexpr wchar_t executable_name[] = L"aurelia_windows_x86_64.exe";
#elif defined(__aarch64__)
inline constexpr wchar_t asset_name[] = L"aurelia_linux_arm64";
inline constexpr wchar_t executable_name[] = L"aurelia";
#else
inline constexpr wchar_t asset_name[] = L"aurelia_linux_x86_64";
inline constexpr wchar_t executable_name[] = L"aurelia";
#endif
using Cancel = std::function<bool()>;
using Event = std::function<void(const json::Value&)>;
enum class Command {
    health, login, logout, reconnect, library, libraries, info, achievements, friends, presence,
    chat_history, chat_send, chat_live, install, downloads, cancel_install, update, verify, uninstall,
    play, launch_targets, running, stop, available, cloud_list, cloud_sync, dlc, enable_dlc, disable_dlc,
    manifests, pin, unpin, downgrade, branches, set_branch, move,
    workshop_browse, workshop_status, workshop_install, workshop_subscribe, workshop_unsubscribe,
    standalone // Host-side workflow; never passed directly to the CLI.
};
struct Request {
    Command command = Command::health;
    uint32_t app = 0, depot = 0;
    uint64_t id = 0; // SteamID, manifest ID or Workshop ID; never a floating-point conversion.
    // Free text of the command (chat message, branch, presence, cloud resolution). Linux, install
    // and play: the build, "linux" (native) or "windows" (through Aurelia's Proton/Wine); empty:
    // install a native build when the game has one, play what launchStandalone selected.
    std::wstring text;
    std::filesystem::path folder;
    bool steam = true;
    std::wstring language = L"english";
    enum class Direction { both, down, up } direction = Direction::both;
    bool auto_cloud = false;
    bool force = false; // stop: end the recorded process tree at once (taskkill /F), as Zamknij grę does.
};
struct Reply {
    bool ok = false, cancelled = false;
    json::Value data;
    std::wstring error;
};
struct InstallationInfo { std::filesystem::path executable; std::wstring version; };
struct Release { std::wstring version, url; uint64_t size = 0; std::string sha256; };
struct Runtime { std::filesystem::path executable, profile; uint32_t query_timeout_ms = 90000; };
std::wstring daemonEndpoint(const std::filesystem::path& profile);
using Transport = std::function<Reply(const Request&, const Cancel&, const Event&)>;
std::vector<std::wstring> arguments(const Request& request);
Reply invoke(const Runtime& runtime, const Request& request, const Cancel& cancelled, const Event& event = {});
Transport transport(Runtime runtime);
Reply checked(const Request& request, Reply reply);
Reply launchStandalone(const Transport& invoke, Request request, const Cancel& cancel, const Event& event = {});
bool saveLibraryCache(const std::filesystem::path& folder, uint64_t account, const json::Value& games);
std::optional<json::Value> loadLibraryCache(const std::filesystem::path& folder, uint64_t account);
InstallationInfo installed(const std::filesystem::path& root);
std::optional<Release> parseRelease(std::string_view body);
bool allowedDownload(std::wstring_view url);
Reply installRelease(const std::filesystem::path& root, const Release& release, const Cancel& cancel,
                     const std::function<void(uint64_t, uint64_t)>& progress = {});
// Versioned executables are published with an atomic pointer; an update never replaces an in-use EXE.
bool publish(const std::filesystem::path& root, const Release& release, const std::filesystem::path& downloaded);

std::optional<uint64_t> integer(const json::Value* value);
std::string text(const json::Value& value, std::string_view key);
bool flag(const json::Value& value, std::string_view key);
std::optional<std::vector<Game>> library(const json::Value& value, uint64_t account);
FriendsResult friends(const json::Value& value);
SteamAchievements achievements(const json::Value& value);
GameDetails details(const json::Value& value);
struct Download {
    uint32_t app = 0; std::wstring name, status; Installation progress;
    uint64_t speed = 0;
    std::optional<uint64_t> eta;
};
std::optional<std::vector<Download>> downloads(const json::Value& value);
enum class CloudState { unknown, checking, files, empty, syncing, synced, conflicts, incomplete, failed };
struct Cloud {
    CloudState state = CloudState::unknown; size_t files = 0, conflicts = 0; uint64_t checked = 0; std::wstring error;
    std::vector<std::wstring> issues;
};
Cloud cloud(const Reply& reply, bool sync);
const wchar_t* cloudLabel(CloudState state);
const wchar_t* cloudShort(CloudState state); // The state alone (a row's value, the icon's tooltip).
struct Dlc { uint32_t app = 0; std::wstring name; std::optional<bool> owned, installed, disabled; };
std::optional<std::vector<Dlc>> dlcs(const json::Value& value);
struct ChatMessage { uint64_t timestamp = 0; bool self = false; std::wstring text; };
std::optional<std::vector<ChatMessage>> chatHistory(const json::Value& value);
bool validGame(const Game& game);
}
