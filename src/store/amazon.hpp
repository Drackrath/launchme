#pragma once
// Amazon Games through Nile (https://github.com/imLinguin/nile, GPL-3.0): an optional helper
// Launchme downloads only when asked (pinned v1.2.0, SHA-256 verified) and runs hidden with its
// own private configuration (NILE_CONFIG_PATH = <config>\amazon), never the user's Nile or the
// Amazon Games app. Nile signs in (the browser shows Amazon's page, the address it lands on is
// pasted back), syncs the library, downloads, verifies and uninstalls; Launchme reads Nile's files
// for the library and starts installed games itself from their fuel.json, with the variables the
// Amazon Games SDK expects (as `nile launch` does).
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

namespace amazon {
using Cancel = std::function<bool()>;

inline constexpr std::wstring_view helper_version = L"1.2.0";
inline constexpr uint64_t helper_bytes = 12386046;

// `nile auth --login --non-interactive`: the sign-in page and what `nile register` needs after it.
struct Login {
    std::string client_id, code_verifier, serial;
    std::wstring url;
};
std::optional<Login> parseLogin(std::string_view output);
// The authorization code from the pasted address Amazon redirected to (or the bare code).
std::optional<std::string> authorizationCode(std::wstring_view pasted);
bool validProductId(std::string_view id); // amzn1.adg.product.<uuid>
// A folder name for the game's installation: its title without the characters Windows forbids.
std::wstring folderName(std::wstring_view title, std::string_view id);

// Nile's files: <config>\amazon\nile\*.json.
std::filesystem::path profileDirectory(const std::filesystem::path& config); // NILE_CONFIG_PATH
std::filesystem::path nileDirectory(const std::filesystem::path& config);    // Its "nile" folder.
std::filesystem::path helperPath(const std::filesystem::path& config);
bool helperInstalled(const std::filesystem::path& config); // Present with the pinned size (hashed when used).

struct Account { std::string user_id; std::wstring name; };
std::optional<Account> parseAccount(std::string_view current_user_json);
// Library entitlements and installs: games owned by `account` (empty: signed out, only the ones
// installed here), installed ones with their fuel.json action.
std::vector<Game> parseLibrary(std::string_view library_json, std::string_view installed_json, const std::string& account,
                               const std::wstring& display_name, const std::filesystem::path& nile_directory);
// fuel.json (JSON5) of an installed game: its executable, arguments and working folder inside the
// installation; nullopt for anything escaping it.
std::optional<GameAction> fuelAction(std::string_view fuel, const std::filesystem::path& install);
// Nile's progress line "= Progress: 12.34 1234/5678, …": downloaded and total bytes.
std::optional<std::pair<uint64_t, uint64_t>> progressBytes(std::string_view line);

struct Profile {
    std::optional<Account> account;
    std::vector<Game> games;
    uint64_t synced_at = 0;
};
Profile readProfile(const std::filesystem::path& config); // Files only; no helper, no network.

enum class State { disconnected, preparing, signing_in, verifying, syncing, connected, error };
enum class Failure { none, cancelled, helper, network, invalid, storage };
enum class Command { restore, download, connect, complete, sync, cancel, logout };
struct Snapshot {
    State state = State::disconnected;
    Failure failure = Failure::none;
    std::string account;
    std::wstring name;
    uint64_t synced_at = 0;
    size_t games = 0;
    std::wstring browser_url; // Sign-in: opened once by the UI.
    bool helper = false;      // Nile is downloaded.
    bool downloading = false; uint64_t download_done = 0, download_total = 0;
};

// The helper's side, replaceable in tests: fetch it (download allowed or only the present copy)
// and run it with the private profile.
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
    virtual void submit(Command, std::wstring input = {}) = 0;
    virtual std::optional<Snapshot> take() = 0;
    virtual bool install(const Game&, gog::InstallRequest) = 0;
    virtual std::optional<gog::InstallSnapshot> takeInstall() = 0;
    virtual void cancelOperation() = 0;
    virtual bool busy() const = 0;
};
std::unique_ptr<Service> makeService(std::filesystem::path config, Operations = nativeOperations());

// "Odinstaluj" for a game Nile installed: Nile deletes it and forgets it. Worker only.
bool uninstall(const std::filesystem::path& config, const Game& game, const Cancel&, const Operations& = nativeOperations());
std::wstring statusText(const Snapshot&);
}
