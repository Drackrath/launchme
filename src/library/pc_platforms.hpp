#pragma once
#include "store_adapter.hpp"
#include "multimc.hpp"

#include <functional>
#include <string_view>

struct NativeInstall {
    GameSource source = GameSource::steam;
    std::string id;
    std::wstring name;
    std::filesystem::path directory;
};
struct InstalledProgram {
    std::wstring key, name, publisher, uninstall;
    std::filesystem::path directory;
};
// A Gaming Services (GDK) title installed for the current user.
struct XboxInstall {
    std::string family;      // Package family name: the game ID.
    std::wstring name;       // ShellVisuals DefaultDisplayName.
    std::wstring app_id;     // Application ID in the package: shell:AppsFolder\<family>!<app_id>.
    std::filesystem::path directory;
};
struct PcDiscovery {
    std::vector<StoreClient> clients;
    std::vector<NativeInstall> installs;
    std::vector<InstalledProgram> programs;
    std::vector<XboxInstall> xbox;
    std::vector<multimc::Installation> multimc; // MultiMC / Prism Launcher / PolyMC (Minecraft instances).
    std::filesystem::path epic_manifests, epic_installed, battlenet_database, battlenet_aggregate;
    // Ubisoft Connect cache roots (ownership/, settings/) and Launchme's title cache (names.tsv).
    std::vector<std::filesystem::path> ubisoft_caches;
    std::filesystem::path ubisoft_names;
};

// multimc_chosen: launcher EXEs or folders the user pointed at (portable MultiMC).
PcDiscovery discoverPcInstallations(const std::vector<std::filesystem::path>& multimc_chosen = {});
StoreLibrary scanNativePcLibraries(const PcDiscovery& discovery, const std::vector<Game>& previous_battlenet = {});
StoreLibrary mergePcLibraries(StoreLibrary native, const std::filesystem::path& catalogs);
StoreLibrary mergeGogLibrary(StoreLibrary native, const std::vector<Game>& owned, const std::string& account);
StoreLibrary mergeEpicLibrary(StoreLibrary native, const std::vector<Game>& owned, const std::string& account, const std::vector<Game>& installed = {});
// Owned games imported from a store's website (EA, Battle.net: store/web_import.hpp). Local
// installations keep precedence; the rest are listed as not installed, installed via the client.
StoreLibrary mergeImportedLibrary(StoreLibrary native, const std::vector<Game>& imported);
// Amazon Games from Nile's files (store/amazon.hpp): the account's games, the installed ones startable.
StoreLibrary mergeAmazonLibrary(StoreLibrary native, const std::vector<Game>& games);
// The Xbox account's games (store/xbox_account.hpp): an Xbox app installation of the same package
// family keeps its entry and gains the account's product and art; the rest are listed, Instaluj
// opening the Xbox app's page (or the Microsoft Store's without the app).
StoreLibrary mergeXboxLibrary(StoreLibrary native, const std::vector<Game>& games);
StoreLibrary scanPcLibraries(const std::filesystem::path& catalogs, const std::vector<Game>& gog_owned = {}, const std::string& gog_account = {},
                             const std::vector<Game>& previous_battlenet = {}, const std::vector<Game>& epic_owned = {},
                             const std::string& epic_account = {}, const std::vector<Game>& epic_installed = {},
                             const std::vector<Game>& imported = {}, const std::vector<std::filesystem::path>& multimc_chosen = {},
                             const std::vector<Game>& amazon = {}, const std::vector<Game>& xbox = {});
GameAction connectPcClient(GameSource source, const std::vector<StoreClient>& clients);
// How Windows itself would remove this installed game (Ustawienia → Aplikacje): the uninstall
// entry its installer registered, matched by the store's key (GOG "<id>_is1") or by its install
// folder. Steam's own steam://uninstall/<id> (Steam asks first). Empty when none is known:
// the launcher then never guesses a command.
GameAction uninstallAction(const Game& game, const std::vector<InstalledProgram>& programs);
// GOG's installers are Inno Setup: its uninstaller (an absolute ...\unins000.exe) also runs
// without a window. silentUninstallAction adds /VERYSILENT /SUPPRESSMSGBOXES /NORESTART to the
// registered command; empty for anything that is not an Inno Setup uninstaller.
bool innoUninstaller(const GameAction& action);
GameAction silentUninstallAction(const GameAction& interactive);
// Runs a silent Inno uninstaller (Windows asks for elevation when the game needs it) and waits
// until it has removed itself: the end of Inno's second phase. refused: UAC was declined.
enum class UninstallResult { done, failed, refused };
UninstallResult runSilentUninstaller(const GameAction& action, void* owner, const std::function<bool()>& cancel);
// The registered uninstall entries (both registry views, machine and user); a worker or a
// user's explicit request reads them, never a frame.
std::vector<InstalledProgram> installedPrograms();
// Epic library entries that are applications, not games, and slip past the catalog categories
// (e.g. the Twinmotion trial): never listed, from the account or from local manifests.
bool epicExcludedTitle(std::wstring_view name);
