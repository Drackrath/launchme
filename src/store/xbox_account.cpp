#include "xbox_account.hpp"
#include "../net/http_download.hpp"
#include "../net/json.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/updater.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <sddl.h>
#else
#include "../library/xdg_paths.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/system.hpp"
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace xbox_account {
namespace {
using text_encoding::utf8;
using text_encoding::wide;
using text_encoding::wideLenient;

#ifdef _WIN32
// The helper is published with Launchme's releases (tools/build-xbox-helper.ps1 prints both values).
constexpr auto helper_digest = "2069fd6578280bfeaf018bb5c5c1805f248e5bcc22b095ebbd49729cc826fd4b";
constexpr auto helper_url = L"https://github.com/uxandai/launchme/releases/download/xbox-helper-v0.1.0/launchme-xbox-0.1.0.exe";
#endif
constexpr std::wstring_view marker_name = L".launchme-xbox.json";
constexpr std::wstring_view header_name = L".xodus-streaming.msixvc";

std::string field(const json::Value& value, const char* key) { const auto* p = value.get(key); return p ? p->stringOr() : std::string{}; }
std::string field(const json::Value* value, const char* key) { return value ? field(*value, key) : std::string{}; }
std::string readText(const std::filesystem::path& path, size_t limit = 32 * 1024 * 1024) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0 || in.tellg() > static_cast<std::streamoff>(limit)) return {};
    std::string text(static_cast<size_t>(in.tellg()), '\0'); in.seekg(0);
    if (!in.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    return text;
}
bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path part = path.wstring() + L".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size()))) return false;
    }
#ifdef _WIN32
    return MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    return atomic_file::replace(part, path);
#endif
}
bool regular(const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); }
bool folder(const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_directory(path, ec); }
bool token(std::string_view text, size_t minimum, size_t maximum, std::string_view extra = {}) {
    return text.size() >= minimum && text.size() <= maximum && std::all_of(text.begin(), text.end(), [&](unsigned char c) {
        return (c < 128 && std::isalnum(c)) || extra.find(static_cast<char>(c)) != std::string_view::npos;
    });
}
bool httpsArt(const std::string& url) {
    return url.starts_with("https://") && url.size() < 2048 && url.find_first_of(" \"\r\n\t") == std::string::npos;
}
// A path inside `root` named by a relative part of an untrusted file; a reparse point must not lead out.
#ifdef _WIN32
std::filesystem::path childPath(const std::filesystem::path& root, std::wstring relative) {
    std::replace(relative.begin(), relative.end(), L'/', L'\\');
    const std::filesystem::path part(relative);
    if (!root.is_absolute() || part.empty() || part.is_absolute() || part.has_root_name() || part.has_root_directory()) return {};
    for (const auto& segment : part) if (segment == L".." || segment == L"." || segment.native().find(L':') != std::wstring::npos) return {};
    const auto full = root / part;
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec); if (ec) return {};
    const auto canonical = std::filesystem::weakly_canonical(full, ec); if (ec) return {};
    auto r = canonical_root.begin(), p = canonical.begin();
    for (; r != canonical_root.end(); ++r, ++p) if (p == canonical.end() || _wcsicmp(r->c_str(), p->c_str()) != 0) return {};
    return full;
}
#else
// Linux: the package names Windows paths ("Bin\Game.exe"); the separators become '/'.
std::filesystem::path childPath(const std::filesystem::path& root, std::wstring relative) {
    std::replace(relative.begin(), relative.end(), L'\\', L'/');
    const std::filesystem::path part(relative);
    if (!root.is_absolute() || part.empty() || part.is_absolute() || part.has_root_name() || part.has_root_directory()) return {};
    for (const auto& segment : part) if (segment == L".." || segment == L"." || segment.wstring().find(L':') != std::wstring::npos) return {};
    const auto full = root / part;
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec); if (ec) return {};
    const auto canonical = std::filesystem::weakly_canonical(full, ec); if (ec) return {};
    auto r = canonical_root.begin(), p = canonical.begin();
    for (; r != canonical_root.end(); ++r, ++p) if (p == canonical.end() || *r != *p) return {};
    return full;
}
#endif
#ifndef _WIN32
// Linux: the profile holds the account's tokens (0600 files of the helper): this user's alone,
// 0700, owned by them, not a symlink.
bool privateDirectory(const std::filesystem::path& path) {
    std::error_code ec; std::filesystem::create_directories(path, ec); if (ec) return false;
    struct stat status{};
    if (::lstat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != ::getuid()) return false;
    return (status.st_mode & 07777) == 0700 || ::chmod(path.c_str(), 0700) == 0;
}
// Not downloaded on Linux: launchme-xbox is shipped next to the launcher (tools/build-xbox-helper.sh).
std::filesystem::path prepareNative(const std::filesystem::path& config, bool, const Cancel&, const std::function<void(uint64_t)>&) {
    const auto exe = helperPath(config);
    return xdg_paths::executableFile(exe) ? exe : std::filesystem::path{};
}
helper_process::Result invokeNative(const std::filesystem::path& exe, const std::filesystem::path& config, const std::vector<std::wstring>& arguments,
                                    const Cancel& cancel, const std::function<void(std::string_view)>& line) {
    const auto profile = profileDirectory(config);
    if (!privateDirectory(profile)) return {};
    std::vector<std::wstring> full{L"--profile", profile.wstring()};
    full.insert(full.end(), arguments.begin(), arguments.end());
    return helper_process::run(exe, profile, full, {}, cancel, line);
}
// The user's locale (LC_ALL, LC_MESSAGES, LANG): "pl_PL.UTF-8" is the language "pl" and the territory "PL".
std::pair<std::wstring, std::wstring> localeParts() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (!value || !*value) continue;
        std::string locale(value);
        locale = locale.substr(0, locale.find_first_of(".@"));
        const auto underscore = locale.find('_');
        const auto language = locale.substr(0, underscore);
        const auto territory = underscore == std::string::npos ? std::string{} : locale.substr(underscore + 1);
        const auto letters = [](const std::string& text, size_t low, size_t high, bool upper) {
            return text.size() >= low && text.size() <= high && std::all_of(text.begin(), text.end(), [&](char c) { return upper ? (c >= 'A' && c <= 'Z') : (c >= 'a' && c <= 'z'); });
        };
        if (!letters(language, 2, 3, false)) return {};
        return {wide(language), letters(territory, 2, 2, true) ? wide(territory) : std::wstring{}};
    }
    return {};
}
#else
// The profile holds the account's sealed tokens: readable by this user and SYSTEM only.
bool privateDirectory(const std::filesystem::path& path) {
    std::error_code ec; std::filesystem::create_directories(path, ec); if (ec) return false;
    HANDLE token_handle = nullptr; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_handle)) return false;
    DWORD size = 0; GetTokenInformation(token_handle, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> bytes(size);
    const bool user = GetTokenInformation(token_handle, TokenUser, bytes.data(), size, &size) != FALSE;
    CloseHandle(token_handle); if (!user) return false;
    LPWSTR sid = nullptr; if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return false;
    const auto sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(sid) + L")"; LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
    const bool ok = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
    LocalFree(descriptor); return ok;
}
std::filesystem::path prepareNative(const std::filesystem::path& config, bool download, const Cancel& cancel, const std::function<void(uint64_t)>& progress) {
    const auto exe = helperPath(config);
    std::error_code ec;
    if (regular(exe) && std::filesystem::file_size(exe, ec) == helper_bytes && updater::sha256File(exe) == helper_digest) return exe;
    if (!download || cancel()) return {};
    std::filesystem::create_directories(exe.parent_path(), ec); if (ec) return {};
    const std::filesystem::path part = exe.native() + L".part";
    const auto error = httpDownload(helper_url, part, helper_bytes, cancel, progress, [](std::wstring_view url) {
        return url.starts_with(L"https://github.com/uxandai/launchme/releases/download/xbox-helper-v0.1.0/") ||
               url.starts_with(L"https://release-assets.githubusercontent.com/") || url.starts_with(L"https://objects.githubusercontent.com/");
    });
    if (error != DownloadError::none || cancel()) return {};
    if (updater::sha256File(part) != helper_digest) { std::filesystem::remove(part, ec); return {}; }
    if (!MoveFileExW(part.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {};
    return exe;
}
helper_process::Result invokeNative(const std::filesystem::path& exe, const std::filesystem::path& config, const std::vector<std::wstring>& arguments,
                                    const Cancel& cancel, const std::function<void(std::string_view)>& line) {
    const auto profile = profileDirectory(config);
    if (!privateDirectory(profile)) return {};
    std::vector<std::wstring> full{L"--profile", profile.native()};
    full.insert(full.end(), arguments.begin(), arguments.end());
    return helper_process::run(exe, profile, full, {}, cancel, line);
}
#endif
std::filesystem::path accountPath(const std::filesystem::path& config) { return profileDirectory(config) / L"account.json"; }
std::filesystem::path installsPath(const std::filesystem::path& config) { return profileDirectory(config) / L"installs.json"; }
std::vector<OwnInstall> readInstalls(const std::filesystem::path& config) { return parseInstalls(readText(installsPath(config), 4 * 1024 * 1024)); }
std::filesystem::path verifiedExecutable(const OwnInstall& install) {
    if (!folder(install.directory) || !regular(install.directory / header_name)) return {};
    return markerExecutable(readText(install.directory / marker_name, 256 * 1024), install.directory, install.product);
}
std::wstring markerTitle(const std::filesystem::path& directory) {
    const auto root = json::parse(readText(directory / marker_name, 256 * 1024));
    auto name = root ? wideLenient(field(*root, "display_name")) : std::wstring{};
    if (name.empty() || name.size() > 512) name = directory.filename().wstring();
    return name;
}
} // namespace

std::filesystem::path profileDirectory(const std::filesystem::path& config) { return config / L"xbox"; }
std::filesystem::path libraryPath(const std::filesystem::path& config) { return profileDirectory(config) / L"library.json"; }
#ifdef _WIN32
std::filesystem::path helperPath(const std::filesystem::path& config) { return config / L"tools" / (L"launchme-xbox-" + std::wstring(helper_version) + L".exe"); }
bool helperInstalled(const std::filesystem::path& config) {
    std::error_code ec; const auto exe = helperPath(config);
    return regular(exe) && std::filesystem::file_size(exe, ec) == helper_bytes;
}
#else
// Linux: next to the launcher (the AppImage ships it); LAUNCHME_XBOX_HELPER names another build.
std::filesystem::path helperPath(const std::filesystem::path&) {
    if (const auto chosen = xdg_paths::variable(L"LAUNCHME_XBOX_HELPER"); !chosen.empty()) return chosen;
    const auto launcher = system_info::executablePath();
    return launcher.empty() ? std::filesystem::path{} : launcher.parent_path() / "launchme-xbox";
}
bool helperInstalled(const std::filesystem::path& config) { return xdg_paths::executableFile(helperPath(config)); }
#endif

bool validProductId(std::string_view id) { return token(id, 9, 16); }
bool validFamilyName(std::string_view family) {
    const auto underscore = family.rfind('_');
    return token(family, 5, 96, "-_.") && underscore != std::string_view::npos && underscore > 0 && underscore + 1 < family.size();
}

std::wstring folderName(std::wstring_view title, std::string_view product) {
    std::wstring name;
    for (const wchar_t c : title) {
        if (c < 32 || std::wstring_view(L"<>:\"/\\|?*\u2122\u00ae").find(c) != std::wstring_view::npos) continue;
        name += c;
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(0, 1);
    if (name.size() > 80) name.resize(80);
    if (name.empty()) name = wide(product);
    return name;
}

#ifdef _WIN32
std::wstring market() {
    wchar_t geo[16]{};
    if (GetUserDefaultGeoName(geo, static_cast<int>(std::size(geo))) == 3 && std::iswalpha(geo[0]) && std::iswalpha(geo[1]))
        return {static_cast<wchar_t>(std::towupper(geo[0])), static_cast<wchar_t>(std::towupper(geo[1]))};
    return L"US";
}
std::wstring language() {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 1) {
        const std::wstring name(locale);
        if (name.size() <= 16 && std::all_of(name.begin(), name.end(), [](wchar_t c) { return std::iswalnum(c) || c == L'-'; })) return name;
    }
    return L"en-US";
}
#else
std::wstring market() {
    const auto [lang, region] = localeParts();
    return region.empty() ? L"US" : region;
}
std::wstring language() {
    const auto [lang, region] = localeParts();
    if (lang.empty()) return L"en-US";
    return region.empty() ? lang : lang + L'-' + region;
}
#endif

std::optional<Result> parseResult(std::string_view output) {
    // The answer is the last line that is a JSON object.
    while (!output.empty()) {
        const auto end = output.find_last_not_of(" \r\n\t");
        if (end == std::string_view::npos) return {};
        output = output.substr(0, end + 1);
        const auto start = output.rfind('\n');
        const auto line = start == std::string_view::npos ? output : output.substr(start + 1);
        if (line.starts_with('{')) {
            const auto root = json::parse(line, 1024 * 1024);
            if (root && root->type == json::Value::Type::object) {
                Result result;
                const auto* ok = root->get("ok");
                result.ok = ok && ok->type == json::Value::Type::boolean && ok->flag;
                result.error = field(*root, "error");
                result.message = wideLenient(field(*root, "message"));
                if (result.message.size() > 2048) result.message.resize(2048);
                result.username = field(*root, "username");
                result.puid = field(*root, "puid");
                result.executable = field(*root, "executable");
                if (!result.ok && !token(result.error, 1, 40, "_")) result.error = "internal";
                return result;
            }
        }
        if (start == std::string_view::npos) return {};
        output = output.substr(0, start);
    }
    return {};
}

std::optional<Progress> parseProgress(std::string_view line) {
    if (!line.starts_with('{') || line.size() > 4096) return {};
    const auto root = json::parse(line);
    const auto* progress = root ? root->get("progress") : nullptr;
    if (!progress || progress->type != json::Value::Type::object) return {};
    Progress result{field(*progress, "stage")};
    const auto number = [&](const char* key) -> std::optional<uint64_t> {
        const auto* value = progress->get(key);
        if (!value || value->type != json::Value::Type::number || !std::isfinite(value->numeric) || value->numeric < 0 || value->numeric > 1e18) return {};
        return static_cast<uint64_t>(value->numeric);
    };
    const auto done = number("done"), total = number("total");
    if (!token(result.stage, 1, 32, "_-") || !done || !total || (*total && *done > *total)) return {};
    result.done = *done; result.total = *total;
    return result;
}

std::optional<Account> parseAccount(std::string_view account_json) {
    const auto root = json::parse(account_json);
    if (!root || root->type != json::Value::Type::object) return {};
    Account account{field(*root, "username"), field(*root, "puid")};
    if (!token(account.puid, 4, 64) || account.username.empty() || account.username.size() > 320 ||
        account.username.find_first_of("\r\n\t\"") != std::string::npos) return {};
    return account;
}

std::vector<OwnInstall> parseInstalls(std::string_view installs_json) {
    std::vector<OwnInstall> result;
    const auto root = json::parse(installs_json, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return result;
    std::set<std::string> seen;
    for (const auto& entry : root->elements) {
        OwnInstall install{field(entry, "product_id"), field(entry, "source_id"), std::filesystem::path(wide(field(entry, "directory")))};
        if (!validProductId(install.product) || !(validFamilyName(install.source_id) || install.source_id == install.product) ||
            !install.directory.is_absolute() || !seen.insert(install.product).second) continue;
        result.push_back(std::move(install));
        if (result.size() >= 4096) break;
    }
    return result;
}

std::string serializeInstalls(const std::vector<OwnInstall>& installs) {
    auto root = json::array();
    for (const auto& install : installs) {
        auto entry = json::object();
        entry.properties["product_id"] = json::string(install.product);
        entry.properties["source_id"] = json::string(install.source_id);
        entry.properties["directory"] = json::string(utf8(install.directory.wstring()));
        root.elements.push_back(std::move(entry));
    }
    return json::serialize(root);
}

std::filesystem::path markerExecutable(std::string_view marker_json, const std::filesystem::path& directory, const std::string& product) {
    const auto root = json::parse(marker_json);
    if (!root || root->type != json::Value::Type::object || field(*root, "product_id") != product) return {};
    const auto exe = childPath(directory, wide(field(*root, "executable")));
#ifdef _WIN32
    if (exe.empty() || !regular(exe) || _wcsicmp(exe.extension().c_str(), L".exe") != 0) return {};
#else
    auto extension = exe.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (exe.empty() || !regular(exe) || extension != L".exe") return {};
#endif
    return exe;
}

bool ownInstall(const Game& game) {
    return game.source == GameSource::xbox && game.installed && game.install_directory.is_absolute() && regular(game.install_directory / marker_name);
}

#ifdef _WIN32
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory) {
    GameAction action{helperPath(config).native(), {}, directory, ActionKind::play};
    for (const auto& part : std::vector<std::wstring>{L"--profile", profileDirectory(config).native(), L"run", L"--dir", directory.native(), L"--market", market()}) {
        if (!action.arguments.empty()) action.arguments += L' ';
        action.arguments += command_line::quoteArgument(part);
    }
    return action;
}

GameAction xboxAppInstall(const std::string& product, bool xbox_app) {
    if (!validProductId(product)) return {};
    if (xbox_app) return {L"msxbox://game/?productId=" + wide(product), {}, {}, ActionKind::install};
    return {L"ms-windows-store://pdp/?ProductId=" + wide(product), {}, {}, ActionKind::open_client};
}
#else
namespace {
std::vector<std::wstring> runArguments(const std::filesystem::path& config, const std::filesystem::path& directory) {
    return {L"--profile", profileDirectory(config).wstring(), L"run", L"--dir", directory.wstring(), L"--market", market()};
}
// GameAction::wrapper, when the launcher's model has it: a native program the Linux runner hands
// the whole Wine/Proton command it built for the action's Windows target to (appended after it).
template <class Action> bool setWrapper(Action& action, std::vector<std::wstring> wrapper) {
    if constexpr (requires { action.wrapper = wrapper; }) { action.wrapper = std::move(wrapper); return true; }
    else return false;
}
}
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory) {
    GameAction action{helperPath(config).wstring(), {}, directory, ActionKind::play};
    for (const auto& part : runArguments(config, directory)) {
        if (!action.arguments.empty()) action.arguments += L' ';
        action.arguments += command_line::quoteArgument(part);
    }
    return action;
}
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory, const std::filesystem::path& executable) {
    GameAction action{executable.wstring(), {}, directory, ActionKind::play};
    auto wrapper = runArguments(config, directory);
    wrapper.insert(wrapper.begin(), helperPath(config).wstring());
    wrapper.push_back(L"--");
    return setWrapper(action, std::move(wrapper)) ? action : runAction(config, directory);
}

// No Xbox app or Microsoft Store on Linux: account games install only through the helper.
GameAction xboxAppInstall(const std::string&, bool) { return {}; }
#endif

std::vector<Game> parseLibrary(std::string_view library_json, const std::vector<OwnInstall>& installs, const std::string& account,
                               const std::filesystem::path& config) {
    std::vector<Game> games;
    std::map<std::string, size_t> by_product;
    const auto root = json::parse(library_json, 32 * 1024 * 1024);
    const auto* list = root ? root->get("games") : nullptr;
    if (!account.empty() && list && list->type == json::Value::Type::array) {
        std::set<std::string> seen;
        for (const auto& entry : list->elements) {
            const auto product = field(entry, "product_id");
            const auto title = wideLenient(field(entry, "title"));
            const auto family = field(entry, "pfn");
            const auto id = validFamilyName(family) ? family : product;
            if (!validProductId(product) || title.empty() || title.size() > 512 || !seen.insert(id).second) continue;
            Game game; game.kind = GameKind::store; game.source = GameSource::xbox; game.source_id = id; game.name = title;
            game.installed = false; game.xbox_product = product; game.xbox_own_install = field(entry, "own_install") == "supported";
            const auto* images = entry.get("images");
            for (const auto* key : {"poster", "box"}) if (const auto art = field(images, key); game.store_tall_art.empty() && httpsArt(art)) game.store_tall_art = art;
            if (const auto art = field(images, "hero"); httpsArt(art)) game.store_wide_art = art;
            if (const auto art = field(images, "logo"); httpsArt(art)) game.store_logo_art = art;
            game.install_action = xboxAppInstall(product, false);
            game.account_id = account;
            by_product.emplace(product, games.size());
            games.push_back(std::move(game));
        }
    }
    for (const auto& install : installs) {
        const auto exe = verifiedExecutable(install);
        if (exe.empty()) continue;
        auto found = by_product.find(install.product);
        if (found == by_product.end()) {
            // Signed out, or no longer in the library (an ended Game Pass): still on disk and listed;
            // Graj asks for a license and says why it cannot start.
            Game game; game.kind = GameKind::store; game.source = GameSource::xbox; game.source_id = install.source_id;
            game.name = markerTitle(install.directory); game.xbox_product = install.product; game.xbox_own_install = true;
            if (!account.empty()) game.account_id = account;
            found = by_product.emplace(install.product, games.size()).first;
            games.push_back(std::move(game));
        }
        auto& game = games[found->second];
        game.installed = true;
        game.install_directory = install.directory;
        game.executable = exe;
#ifdef _WIN32
        game.play_action = runAction(config, install.directory);
#else
        game.play_action = runAction(config, install.directory, exe);
#endif
    }
    return games;
}

Profile readProfile(const std::filesystem::path& config) {
    Profile profile;
    profile.account = parseAccount(readText(accountPath(config), 64 * 1024));
    const auto library = readText(libraryPath(config));
    profile.games = parseLibrary(library, readInstalls(config), profile.account ? profile.account->puid : std::string{}, config);
    if (const auto root = json::parse(library, 32 * 1024 * 1024)) {
        if (const auto* synced = root->get("synced_at"); synced && synced->type == json::Value::Type::number && std::isfinite(synced->numeric) && synced->numeric > 0)
            profile.synced_at = static_cast<uint64_t>(synced->numeric);
        if (const auto pass = field(*root, "game_pass"); token(pass, 4, 32)) profile.game_pass = pass;
    }
    return profile;
}

Operations nativeOperations() { return {prepareNative, invokeNative}; }

namespace {
gog::InstallError installError(const std::string& error) {
    if (error == "not_entitled" || error == "signed_out") return gog::InstallError::account;
    if (error == "unsupported_package" || error == "not_found") return gog::InstallError::unavailable;
    if (error == "storage") return gog::InstallError::storage;
    if (error == "network") return gog::InstallError::network;
    return gog::InstallError::installer;
}

class HelperService final : public Service {
    std::filesystem::path config_;
    Operations operations_;
    struct Work { Command command; uint64_t generation; std::optional<Game> game; std::optional<gog::InstallRequest> request; };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Work> queue_;
    std::atomic_uint64_t generation_{0};
    std::atomic_bool stop_{false}, busy_{false}, operation_cancel_{false};
    std::optional<Snapshot> result_;
    gog::InstallSnapshot install_;
    bool install_dirty_ = false;
    std::thread worker_;

    Snapshot profileSnapshot(Failure failure = Failure::none) const {
        const auto profile = readProfile(config_);
        Snapshot s; s.failure = failure; s.helper = helperInstalled(config_);
        if (profile.account) {
            s.state = State::connected; s.account = profile.account->puid; s.name = wideLenient(profile.account->username);
            s.games = static_cast<size_t>(std::count_if(profile.games.begin(), profile.games.end(), [](const Game& g) { return !g.account_id.empty(); }));
            s.synced_at = profile.synced_at; s.game_pass = !profile.game_pass.empty();
        }
        if (failure != Failure::none && failure != Failure::cancelled && !profile.account) s.state = State::error;
        return s;
    }
    void publish(uint64_t generation, Snapshot s) {
        std::lock_guard lock(mutex_);
        if (!stop_ && generation == generation_) result_ = std::move(s);
    }
    void publishState(uint64_t generation, State state) { auto s = profileSnapshot(); s.state = state; publish(generation, std::move(s)); }
    std::filesystem::path helper(uint64_t generation, bool download, const Cancel& cancel) {
        return operations_.prepare(config_, download, cancel, [&](uint64_t done) {
            auto s = profileSnapshot(); s.state = State::preparing; s.downloading = true; s.download_done = done; s.download_total = helper_bytes;
            publish(generation, std::move(s));
        });
    }
    Result call(const std::filesystem::path& exe, const std::vector<std::wstring>& arguments, const Cancel& cancel,
                const std::function<void(std::string_view)>& line = {}) {
        auto process = operations_.invoke(exe, config_, arguments, cancel, line);
        auto result = parseResult(process.output);
        SecureZeroMemory(process.output.data(), process.output.size());
        if (!process.started) return {false, "helper"};
        if (!result) return {false, cancel() ? "cancelled" : "internal"};
        return *result;
    }
    void signOutLocally() {
        std::error_code ec;
        std::filesystem::remove(accountPath(config_), ec);
        std::filesystem::remove(libraryPath(config_), ec);
    }
    Failure failureOf(const Result& result) {
        if (result.error == "helper") return Failure::helper;
        if (result.error == "cancelled") return Failure::cancelled;
        if (result.error == "signed_out") return Failure::expired;
        if (result.error == "storage") return Failure::storage;
        return Failure::network;
    }
    // `library`: the helper writes the file only after a complete sync.
    Failure sync(const std::filesystem::path& exe, const Cancel& cancel) {
        const auto result = call(exe, {L"library", L"--out", libraryPath(config_).wstring(), L"--market", market(), L"--language", language()}, cancel);
        if (result.ok) return Failure::none;
        if (result.error == "signed_out") signOutLocally(); // The session expired: sign in again.
        return failureOf(result);
    }
    void progress(gog::InstallState state, uint64_t done = 0, uint64_t total = 0, gog::InstallError error = gog::InstallError::none) {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        install_.state = state; install_.done = done; install_.total = total; install_.error = error; install_dirty_ = true;
    }
    void installGame(const Work& work) {
        const auto& game = *work.game;
        const auto& request = *work.request;
        const Cancel cancel = [&] { return stop_.load() || operation_cancel_.load(); };
        progress(gog::InstallState::loading);
        const auto profile = readProfile(config_);
        const auto owned = std::find_if(profile.games.begin(), profile.games.end(), [&](const Game& g) { return g.xbox_product == game.xbox_product && !g.account_id.empty(); });
        if (!profile.account || owned == profile.games.end()) { progress(gog::InstallState::error, 0, 0, gog::InstallError::account); return; }
        if (!owned->xbox_own_install || !request.install_directory.is_absolute()) { progress(gog::InstallState::error, 0, 0, gog::InstallError::unavailable); return; }
        const auto exe = helper(work.generation, false, cancel);
        if (exe.empty()) { progress(gog::InstallState::error, 0, 0, gog::InstallError::installer); return; }
        std::error_code ec; std::filesystem::create_directories(request.install_directory, ec);
        if (ec) { progress(gog::InstallState::error, 0, 0, gog::InstallError::storage); return; }
        const auto result = call(exe, {L"install", L"--product", wide(game.xbox_product), L"--dest", request.install_directory.wstring(), L"--market", market()}, cancel,
            [&](std::string_view line) {
                const auto step = parseProgress(line);
                if (!step) return;
                if (step->stage == "download") progress(gog::InstallState::downloading, step->done, step->total);
                else progress(gog::InstallState::loading);
            });
        if (cancel()) { progress(gog::InstallState::cancelled); return; }
        if (!result.ok) { progress(gog::InstallState::error, 0, 0, installError(result.error)); return; }
        // The helper exited 0: its marker and package header must be there, naming an executable inside.
        progress(gog::InstallState::verifying);
        OwnInstall install{owned->xbox_product, owned->source_id, request.install_directory};
        if (verifiedExecutable(install).empty()) { progress(gog::InstallState::error, 0, 0, gog::InstallError::detection); return; }
        auto installs = readInstalls(config_);
        std::erase_if(installs, [&](const OwnInstall& i) { return i.product == install.product; });
        installs.push_back(std::move(install));
        if (!writeText(installsPath(config_), serializeInstalls(installs))) { progress(gog::InstallState::error, 0, 0, gog::InstallError::storage); return; }
        progress(gog::InstallState::completed);
    }
    void process(Work& work) {
        const auto generation = work.generation;
        const Cancel cancel = [&] { return stop_.load() || generation != generation_.load(); };
        if (work.game) { installGame(work); return; }
        switch (work.command) {
        case Command::restore: publish(generation, profileSnapshot()); break;
        case Command::download: {
            publishState(generation, State::preparing);
            const auto exe = helper(generation, true, cancel);
            if (cancel()) return;
            publish(generation, profileSnapshot(exe.empty() ? Failure::helper : Failure::none));
            break;
        }
        case Command::connect: {
            publishState(generation, State::preparing);
            const auto exe = helper(generation, true, cancel);
            if (cancel()) return;
            if (exe.empty()) { auto s = profileSnapshot(Failure::helper); s.state = State::error; publish(generation, std::move(s)); return; }
            // "Zmień konto": the previous session goes first (the device registration stays).
            if (readProfile(config_).account) { call(exe, {L"logout"}, cancel); signOutLocally(); }
            if (cancel()) return;
            // The helper's own window shows Microsoft's sign-in; closing it cancels.
            publishState(generation, State::signing_in);
            const auto login = call(exe, {L"login"}, cancel);
            if (cancel()) return;
            if (!login.ok) {
                auto s = profileSnapshot(failureOf(login));
                s.state = login.error == "cancelled" ? State::disconnected : State::error;
                publish(generation, std::move(s)); return;
            }
            auto account = json::object();
            account.properties["username"] = json::string(login.username);
            account.properties["puid"] = json::string(login.puid);
            if (!parseAccount(json::serialize(account)) || !writeText(accountPath(config_), json::serialize(account))) {
                auto s = profileSnapshot(Failure::storage); s.state = State::error; publish(generation, std::move(s)); return;
            }
            publishState(generation, State::syncing);
            const auto failure = sync(exe, cancel);
            if (cancel()) return;
            publish(generation, profileSnapshot(failure));
            break;
        }
        case Command::sync: {
            if (!readProfile(config_).account) { publish(generation, profileSnapshot()); return; }
            publishState(generation, State::syncing);
            const auto exe = helper(generation, false, cancel);
            if (exe.empty()) { publish(generation, profileSnapshot(Failure::helper)); return; }
            const auto failure = sync(exe, cancel);
            if (cancel()) return;
            publish(generation, profileSnapshot(failure));
            break;
        }
        case Command::cancel: publish(generation, profileSnapshot()); break;
        case Command::logout: {
            if (const auto exe = helper(generation, false, cancel); !exe.empty() && readProfile(config_).account)
                call(exe, {L"logout"}, [&] { return stop_.load(); });
            signOutLocally(); // Gone from Launchme even when the helper is missing.
            publish(generation, profileSnapshot());
            break;
        }
        }
    }
    void run() {
        for (;;) {
            Work work;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                work = std::move(queue_.front()); queue_.pop_front();
                busy_ = true;
            }
            try { process(work); } catch (...) {}
            std::lock_guard lock(mutex_); busy_ = !queue_.empty();
        }
    }
public:
    HelperService(std::filesystem::path config, Operations operations) : config_(std::move(config)), operations_(std::move(operations)) {
        worker_ = std::thread([this] { run(); });
    }
    ~HelperService() override {
        { std::lock_guard lock(mutex_); stop_ = true; queue_.clear(); }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void submit(Command command) override {
        std::lock_guard lock(mutex_);
        // A new sign-in, a cancel or a sign-out supersedes what was running (installs keep theirs).
        if (command == Command::connect || command == Command::cancel || command == Command::logout) {
            ++generation_;
            std::erase_if(queue_, [](const Work& w) { return !w.game; });
        }
        queue_.push_back({command, generation_.load(), {}, {}});
        wake_.notify_one();
    }
    std::optional<Snapshot> take() override {
        std::lock_guard lock(mutex_); auto r = std::move(result_); result_.reset(); return r;
    }
    bool install(const Game& game, gog::InstallRequest request) override {
        std::lock_guard lock(mutex_);
        if (install_.busy() || std::any_of(queue_.begin(), queue_.end(), [](const Work& w) { return w.game.has_value(); })) return false;
        operation_cancel_ = false;
        install_ = {}; install_.product = game.source_id; install_.state = gog::InstallState::loading; install_dirty_ = true;
        queue_.push_back({Command::sync, generation_.load(), game, std::move(request)});
        wake_.notify_one();
        return true;
    }
    std::optional<gog::InstallSnapshot> takeInstall() override {
        std::lock_guard lock(mutex_);
        if (!install_dirty_) return {};
        install_dirty_ = false; return install_;
    }
    void cancelOperation() override { operation_cancel_ = true; }
    bool busy() const override {
        std::lock_guard lock(mutex_);
        return busy_ || !queue_.empty() || install_.busy();
    }
};
} // namespace

std::unique_ptr<Service> makeService(std::filesystem::path config, Operations operations) {
    return std::make_unique<HelperService>(std::move(config), std::move(operations));
}

bool uninstall(const std::filesystem::path& config, const Game& game) {
    if (game.source != GameSource::xbox || !validProductId(game.xbox_product) || !game.install_directory.is_absolute()) return false;
    auto installs = readInstalls(config);
    const auto found = std::find_if(installs.begin(), installs.end(), [&](const OwnInstall& i) {
        std::error_code ec;
        return i.product == game.xbox_product && std::filesystem::equivalent(i.directory, game.install_directory, ec);
    });
    // Only a folder the helper installed and Launchme recorded: never an arbitrary path.
    if (found == installs.end() || !regular(found->directory / marker_name)) return false;
    const auto directory = found->directory;
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    if (ec || folder(directory)) return false;
    installs.erase(found);
    return writeText(installsPath(config), serializeInstalls(installs));
}

std::wstring statusText(const Snapshot& s) {
    if (s.downloading) {
        const auto percent = s.download_total ? s.download_done * 100 / s.download_total : 0;
        return tr(L"Pobieranie pomocnika Xbox… ") + std::to_wstring(percent) + L"%";
    }
    switch (s.state) {
    case State::preparing: return tr(L"Przygotowanie logowania Xbox…");
    case State::signing_in: return tr(L"Zaloguj się w oknie Microsoft.");
    case State::syncing: return tr(L"Pobieranie biblioteki Xbox…");
    case State::connected: {
        auto text = (s.name.empty() ? std::wstring{} : s.name + L" · ") + tr(L"gry: ") + std::to_wstring(s.games);
        if (s.game_pass) text += std::wstring(L" · ") + L"Game Pass";
        if (s.failure == Failure::network) text += std::wstring(L" · ") + tr(L"Xbox offline");
        else if (s.failure == Failure::helper) text += std::wstring(L" · ") + tr(L"brak pomocnika");
        return text;
    }
    case State::error:
        switch (s.failure) {
        case Failure::helper: return tr(L"Nie można pobrać ani uruchomić pomocnika Xbox.");
        case Failure::expired: return tr(L"Sesja Xbox wygasła. Połącz konto ponownie.");
        case Failure::storage: return tr(L"Nie można zapisać danych konta Xbox.");
        default: return tr(L"Brak połączenia z Xbox. Spróbuj ponownie.");
        }
    default: return tr(L"Konto niepołączone");
    }
}
}
