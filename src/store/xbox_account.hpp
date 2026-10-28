#pragma once
// The Xbox (Microsoft) account through launchme-xbox, Launchme's own helper built on Xodus
// (https://github.com/xodus-gaming/xodus, GPL-3.0; source in helpers/xbox). Like Nile for Amazon it is
// optional and downloaded only when asked (pinned version, size and SHA-256 verified), and it runs
// hidden with a private profile (<config>\xbox: DPAPI-sealed tokens, the WebView2 data of its
// sign-in window, the synced library). Launchme never touches the Xbox app's own sign-in.
//
// What it gives: the account's games (bought, Xbox Play Anywhere, the PC Game Pass catalog while
// subscribed) next to the games the Xbox app installed. Install goes through the Xbox app by
// default; with "Instalacja przez Launchme" (experimental) the helper streams a GDK title's MSIXVC
// package into a folder and Graj runs it through the helper, which asks for a fresh license,
// decrypts the package's encrypted files for the session and seals them again afterwards.
#include "../model/game.hpp"
#include "../platform/helper_process.hpp"
#include "gog_install.hpp" // The shared install progress/modal DTOs.

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xbox_account {
using Cancel = std::function<bool()>;

inline constexpr std::wstring_view helper_version = L"0.1.0";
inline constexpr uint64_t helper_bytes = 11705856;

std::filesystem::path profileDirectory(const std::filesystem::path& config); // <config>\xbox
std::filesystem::path libraryPath(const std::filesystem::path& config);      // The helper's synced library.
std::filesystem::path helperPath(const std::filesystem::path& config);       // <config>\tools\launchme-xbox-<version>.exe
bool helperInstalled(const std::filesystem::path& config);                   // Present with the pinned size (hashed when used).

bool validProductId(std::string_view id);      // Store BigId, e.g. 9NBLGGH4R315.
bool validFamilyName(std::string_view family); // Package family name: Publisher.Name_publisherhash.
// A folder name for the game's installation: its title without the characters Windows forbids.
std::wstring folderName(std::wstring_view title, std::string_view product);
// The market and language the Store catalog is asked in, from the user's Windows region.
std::wstring market();
std::wstring language();

// The helper's answer: the last JSON object it printed on stdout.
struct Result {
    bool ok = false;
    std::string error;   // usage, signed_out, cancelled, network, not_entitled, unsupported_package, not_found, storage, internal.
    std::wstring message;
    std::string username, puid, executable;
};
std::optional<Result> parseResult(std::string_view output);
// {"progress":{"stage":"download","done":N,"total":M}} from its error stream.
struct Progress { std::string stage; uint64_t done = 0, total = 0; };
std::optional<Progress> parseProgress(std::string_view line);

struct Account { std::string username, puid; };
std::optional<Account> parseAccount(std::string_view account_json);

// Games installed by the helper: <config>\xbox\installs.json, each checked against its marker
// (<dir>\.launchme-xbox.json, written by `install`) and its package header.
struct OwnInstall { std::string product, source_id; std::filesystem::path directory; };
std::vector<OwnInstall> parseInstalls(std::string_view installs_json);
std::string serializeInstalls(const std::vector<OwnInstall>& installs);
// The executable the install marker names, inside `directory`; empty if missing or escaping it.
std::filesystem::path markerExecutable(std::string_view marker_json, const std::filesystem::path& directory, const std::string& product);
bool ownInstall(const Game& game); // Installed by the helper (its marker is in the game's folder).

// The synced library (helper `library`) as Launchme games. `account` empty: signed out, only
// installs made here. Account games use the package family name as their ID (the Xbox app's
// installs use it too, so both meet in one entry); the Store product ID stays in xbox_product.
std::vector<Game> parseLibrary(std::string_view library_json, const std::vector<OwnInstall>& installs, const std::string& account,
                               const std::filesystem::path& config);
// The helper started for Graj of a game it installed.
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory);
// Install through the Xbox app (its product page; the Microsoft Store when the app is missing).
GameAction xboxAppInstall(const std::string& product, bool xbox_app);

struct Profile {
    std::optional<Account> account;
    std::vector<Game> games;
    uint64_t synced_at = 0;
    std::string game_pass; // The active subscription that lists the PC Game Pass catalog.
};
Profile readProfile(const std::filesystem::path& config); // Files only; no helper, no network.

enum class State { disconnected, preparing, signing_in, syncing, connected, error };
enum class Failure { none, cancelled, helper, network, expired, storage };
enum class Command { restore, download, connect, sync, cancel, logout };
struct Snapshot {
    State state = State::disconnected;
    Failure failure = Failure::none;
    std::string account;   // PUID.
    std::wstring name;     // The account's e-mail address.
    uint64_t synced_at = 0;
    size_t games = 0;
    bool game_pass = false;
    bool helper = false;   // Downloaded.
    bool downloading = false; uint64_t download_done = 0, download_total = 0;
};

// The helper's side, replaceable in tests.
struct Operations {
    std::function<std::filesystem::path(const std::filesystem::path& config, bool download, const Cancel&,
                                        const std::function<void(uint64_t)>&)> prepare;
    std::function<helper_process::Result(const std::filesystem::path& exe, const std::filesystem::path& config,
                                         const std::vector<std::wstring>& arguments, const Cancel&,
                                         const std::function<void(std::string_view)>&)> invoke;
};
Operations nativeOperations();

class Service {
public:
    virtual ~Service() = default;
    virtual void submit(Command) = 0;
    virtual std::optional<Snapshot> take() = 0;
    virtual bool install(const Game&, gog::InstallRequest) = 0;
    virtual std::optional<gog::InstallSnapshot> takeInstall() = 0;
    virtual void cancelOperation() = 0;
    virtual bool busy() const = 0;
};
std::unique_ptr<Service> makeService(std::filesystem::path config, Operations = nativeOperations());

// "Odinstaluj" for a game the helper installed: its folder goes and it leaves installs.json. Worker only.
bool uninstall(const std::filesystem::path& config, const Game& game);
std::wstring statusText(const Snapshot&);
}
