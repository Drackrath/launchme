#include "amazon.hpp"
#include "../net/http_download.hpp"
#include "../net/json.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/updater.hpp"

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace amazon {
namespace {
using text_encoding::utf8;
using text_encoding::wideLenient;

constexpr auto helper_digest = "6531790c59f78cea4a8743bf0582d5afda7fb887f5c143391d7339ad0f42ab88";
constexpr auto helper_url = L"https://github.com/imLinguin/nile/releases/download/v1.2.0/nile_windows_x86_64.exe";

std::string field(const json::Value& value, const char* key) { const auto* p = value.get(key); return p ? p->stringOr() : std::string{}; }
const json::Value* child(const json::Value* value, const char* key) { return value ? value->get(key) : nullptr; }
std::string readText(const std::filesystem::path& path, size_t limit = 32 * 1024 * 1024) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0 || in.tellg() > static_cast<std::streamoff>(limit)) return {};
    std::string text(static_cast<size_t>(in.tellg()), '\0'); in.seekg(0);
    if (!in.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    return text;
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
std::filesystem::path childPath(const std::filesystem::path& root, std::wstring relative) {
    std::replace(relative.begin(), relative.end(), L'/', L'\\');
    const std::filesystem::path part(relative);
    if (!root.is_absolute() || part.empty() || part.is_absolute() || part.has_root_name() || part.has_root_directory()) return {};
    for (const auto& segment : part) if (segment == L".." || segment.native().find(L':') != std::wstring::npos) return {};
    const auto full = root / part;
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec); if (ec) return {};
    const auto canonical = std::filesystem::weakly_canonical(full, ec); if (ec) return {};
    auto r = canonical_root.begin(), p = canonical.begin();
    for (; r != canonical_root.end(); ++r, ++p) if (p == canonical.end() || _wcsicmp(r->c_str(), p->c_str()) != 0) return {};
    return full;
}
// JSON5 as fuel.json allows it: comments and trailing commas removed outside strings.
std::string plainJson(std::string_view text) {
    std::string out; out.reserve(text.size());
    bool string = false, escape = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (string) {
            out += c;
            if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') string = false;
            continue;
        }
        if (c == '"') { string = true; out += c; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') { while (i < text.size() && text[i] != '\n') ++i; out += '\n'; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto end = text.find("*/", i + 2); if (end == std::string_view::npos) return {};
            i = end + 1; out += ' '; continue;
        }
        if (c == ']' || c == '}') {
            auto last = out.find_last_not_of(" \t\r\n");
            if (last != std::string::npos && out[last] == ',') out.erase(last, 1);
        }
        out += c;
    }
    return out;
}
std::string percentDecode(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16)); i += 2;
        } else out += text[i] == '+' ? ' ' : text[i];
    }
    return out;
}
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
        return url.starts_with(L"https://github.com/imLinguin/nile/releases/download/v1.2.0/") ||
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
    return helper_process::run(exe, profile, arguments,
        {{L"NILE_CONFIG_PATH", profile.native()}, {L"PYTHONIOENCODING", L"utf-8"}, {L"PYTHONUTF8", L"1"}}, cancel, line);
}
std::map<std::string, std::filesystem::path> installedPaths(std::string_view installed_json) {
    std::map<std::string, std::filesystem::path> result;
    const auto installed = json::parse(installed_json, 8 * 1024 * 1024);
    if (!installed || installed->type != json::Value::Type::array) return result;
    for (const auto& entry : installed->elements) {
        const auto id = field(entry, "id");
        const std::filesystem::path path(wideLenient(field(entry, "path")));
        if (validProductId(id) && path.is_absolute()) result.emplace(id, path);
    }
    return result;
}
}

std::filesystem::path profileDirectory(const std::filesystem::path& config) { return config / L"amazon"; }
std::filesystem::path nileDirectory(const std::filesystem::path& config) { return profileDirectory(config) / L"nile"; }
std::filesystem::path helperPath(const std::filesystem::path& config) { return config / L"tools" / (L"nile-" + std::wstring(helper_version) + L".exe"); }
bool helperInstalled(const std::filesystem::path& config) {
    std::error_code ec; const auto exe = helperPath(config);
    return regular(exe) && std::filesystem::file_size(exe, ec) == helper_bytes;
}

std::optional<Login> parseLogin(std::string_view output) {
    // Nile prints only this JSON on stdout; tolerate a log line before it.
    const auto begin = output.find('{'), end = output.rfind('}');
    if (begin == std::string_view::npos || end == std::string_view::npos || end < begin) return {};
    const auto root = json::parse(output.substr(begin, end - begin + 1));
    if (!root) return {};
    Login login{field(*root, "client_id"), field(*root, "code_verifier"), field(*root, "serial"), wideLenient(field(*root, "url"))};
    if (!token(login.client_id, 16, 256) || !token(login.code_verifier, 16, 256, "-_") || !token(login.serial, 8, 128)) return {};
    if (login.url.size() > 8192 || !(login.url.starts_with(L"https://amazon.com/ap/signin?") || login.url.starts_with(L"https://www.amazon.com/ap/signin?")) ||
        login.url.find_first_of(L" \"\r\n\t<>") != std::wstring::npos) return {};
    return login;
}

std::optional<std::string> authorizationCode(std::wstring_view pasted) {
    if (pasted.empty() || pasted.size() > 16384) return {};
    auto text = utf8(pasted);
    const auto first = text.find_first_not_of(" \r\n\t"), last = text.find_last_not_of(" \r\n\t");
    if (first == std::string::npos) return {};
    text = text.substr(first, last - first + 1);
    constexpr std::string_view key = "openid.oa2.authorization_code=";
    if (const auto at = text.find(key); at != std::string::npos) {
        const auto start = at + key.size();
        text = percentDecode(std::string_view(text).substr(start, text.find('&', start) == std::string::npos ? std::string::npos : text.find('&', start) - start));
    }
    if (!token(text, 8, 512, "-_")) return {};
    return text;
}

bool validProductId(std::string_view id) {
    constexpr std::string_view prefix = "amzn1.adg.product.";
    return id.starts_with(prefix) && token(id.substr(prefix.size()), 8, 64, "-");
}

std::wstring folderName(std::wstring_view title, std::string_view id) {
    std::wstring name;
    for (const wchar_t c : title) {
        if (c < 32 || std::wstring_view(L"<>:\"/\\|?*\u2122\u00ae").find(c) != std::wstring_view::npos) continue;
        name += c;
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(0, 1);
    if (name.size() > 80) name.resize(80);
    if (name.empty()) { const auto tail = id.substr(id.rfind('.') == std::string_view::npos ? 0 : id.rfind('.') + 1); name = text_encoding::wide(tail); }
    return name;
}

std::optional<Account> parseAccount(std::string_view current_user_json) {
    const auto root = json::parse(current_user_json);
    if (!root || root->type != json::Value::Type::object) return {};
    Account account{field(*root, "user_id"), wideLenient(field(*root, "name"))};
    if (!token(account.user_id, 4, 160, ".-_")) return {};
    if (account.name.size() > 256) account.name.resize(256);
    return account;
}

std::optional<GameAction> fuelAction(std::string_view fuel, const std::filesystem::path& install) {
    if (fuel.size() > 1024 * 1024 || !install.is_absolute()) return {};
    const auto root = json::parse(plainJson(fuel));
    const auto* main = root ? root->get("Main") : nullptr;
    if (!main || main->type != json::Value::Type::object) return {};
    const auto command = text_encoding::wide(field(*main, "Command"));
    const auto executable = childPath(install, command);
    if (executable.empty() || !regular(executable)) return {};
    GameAction action{executable.native(), {}, executable.parent_path(), ActionKind::play};
    action.working_directory = install;
    if (const auto override_dir = text_encoding::wide(field(*main, "WorkingSubdirOverride")); !override_dir.empty()) {
        const auto directory = childPath(install, override_dir);
        if (directory.empty() || !folder(directory)) return {};
        action.working_directory = directory;
    }
    if (const auto* args = main->get("Args"); args && args->type != json::Value::Type::null_value) {
        if (args->type != json::Value::Type::array) return {};
        for (const auto& arg : args->elements) {
            if (arg.type != json::Value::Type::string || arg.text.find('\0') != std::string::npos) return {};
            if (!action.arguments.empty()) action.arguments += L' ';
            action.arguments += command_line::quoteArgument(text_encoding::wide(arg.text));
            if (action.arguments.size() > 30000) return {};
        }
    }
    return action;
}

std::vector<Game> parseLibrary(std::string_view library_json, std::string_view installed_json, const std::string& account,
                               const std::wstring& display_name, const std::filesystem::path& nile_directory) {
    std::vector<Game> games;
    const auto library = json::parse(library_json, 32 * 1024 * 1024);
    if (!library || library->type != json::Value::Type::array) return games;
    const auto installed = installedPaths(installed_json);
    const auto sdk = nile_directory / L"SDK" / L"Amazon Games Services";
    std::set<std::string> seen;
    for (const auto& entitlement : library->elements) {
        const auto* product = entitlement.get("product");
        if (!product || product->type != json::Value::Type::object) continue;
        const auto id = field(*product, "id");
        const auto title = wideLenient(field(*product, "title"));
        if (!validProductId(id) || title.empty() || title.size() > 512 || !seen.insert(id).second) continue;
        Game game; game.kind = GameKind::store; game.source = GameSource::amazon; game.source_id = id; game.name = title;
        game.installed = false;
        const auto* detail = product->get("productDetail");
        const auto* details = child(detail, "details");
        if (detail) if (const auto icon = field(*detail, "iconUrl"); httpsArt(icon)) game.store_tall_art = icon;
        if (details) {
            for (const auto* key : {"backgroundUrl1", "backgroundUrl2"})
                if (const auto wide_art = field(*details, key); game.store_wide_art.empty() && httpsArt(wide_art)) game.store_wide_art = wide_art;
            if (const auto logo = field(*details, "logoUrl"); httpsArt(logo)) game.store_logo_art = logo;
        }
        if (const auto found = installed.find(id); found != installed.end() && folder(found->second)) {
            if (auto action = fuelAction(readText(found->second / L"fuel.json", 1024 * 1024), found->second)) {
                const auto entitlement_id = field(entitlement, "id");
                const auto sku = field(*product, "sku");
                // As `nile launch`: the SDK's folders, the entitlement and the account's display name.
                action->environment = {
                    {L"FUEL_DIR", (sdk / L"Legacy").native()},
                    {L"AMAZON_GAMES_SDK_PATH", (sdk / L"AmazonGamesSDK").native()},
                    {L"AMAZON_GAMES_FUEL_ENTITLEMENT_ID", token(entitlement_id, 1, 160, "-_.") ? wideLenient(entitlement_id) : std::wstring{}},
                    {L"AMAZON_GAMES_FUEL_PRODUCT_SKU", token(sku, 1, 160, "-_.") ? wideLenient(sku) : std::wstring{}},
                    {L"AMAZON_GAMES_FUEL_DISPLAY_NAME", display_name},
                };
                game.installed = true;
                game.install_directory = found->second;
                game.executable = action->target;
                game.play_action = std::move(*action);
            }
        }
        if (!game.installed && account.empty()) continue; // Signed out: only what is installed here.
        if (!account.empty()) game.account_id = account;
        games.push_back(std::move(game));
    }
    return games;
}

std::optional<std::pair<uint64_t, uint64_t>> progressBytes(std::string_view line) {
    const auto at = line.find("Progress: ");
    if (at == std::string_view::npos) return {};
    line.remove_prefix(at + 10);
    const auto space = line.find(' '); if (space == std::string_view::npos) return {};
    line.remove_prefix(space + 1);
    uint64_t done = 0, total = 0;
    auto [slash, error] = std::from_chars(line.data(), line.data() + line.size(), done);
    if (error != std::errc{} || slash == line.data() + line.size() || *slash != '/') return {};
    auto [end, second] = std::from_chars(slash + 1, line.data() + line.size(), total);
    if (second != std::errc{} || !total || done > total) return {};
    return std::pair{done, total};
}

Profile readProfile(const std::filesystem::path& config) {
    Profile profile;
    const auto nile = nileDirectory(config);
    profile.account = parseAccount(readText(nile / L"current_user.json", 64 * 1024));
    profile.games = parseLibrary(readText(nile / L"library.json"), readText(nile / L"installed.json", 8 * 1024 * 1024),
                                 profile.account ? profile.account->user_id : std::string{}, profile.account ? profile.account->name : std::wstring{}, nile);
    const auto point = readText(nile / L"syncpoint.raw", 64);
    double seconds = 0;
    if (const auto [end, error] = std::from_chars(point.data(), point.data() + point.size(), seconds); error == std::errc{} && std::isfinite(seconds) && seconds > 0)
        profile.synced_at = static_cast<uint64_t>(seconds);
    return profile;
}

Operations nativeOperations() { return {prepareNative, invokeNative}; }

namespace {
class NileService final : public Service {
    std::filesystem::path config_;
    Operations operations_;
    struct Work { Command command; std::wstring input; uint64_t generation; std::optional<Game> game; std::optional<gog::InstallRequest> request; };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Work> queue_;
    std::atomic_uint64_t generation_{0};
    std::atomic_bool stop_{false}, busy_{false}, operation_cancel_{false};
    std::optional<Snapshot> result_;
    gog::InstallSnapshot install_;
    bool install_dirty_ = false;
    std::optional<Login> login_;
    std::thread worker_;

    Snapshot profileSnapshot(Failure failure = Failure::none) const {
        const auto profile = readProfile(config_);
        Snapshot s; s.failure = failure; s.helper = helperInstalled(config_);
        if (profile.account) {
            s.state = State::connected; s.account = profile.account->user_id; s.name = profile.account->name;
            s.games = profile.games.size(); s.synced_at = profile.synced_at;
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
    void removeCredentials() {
        const auto nile = nileDirectory(config_);
        std::error_code ec;
        std::filesystem::remove(nile / L"current_user.json", ec);
        std::filesystem::remove(nile / L"syncpoint.raw", ec);
        for (std::filesystem::directory_iterator it(nile, ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == L".enc") std::filesystem::remove(it->path(), ec);
    }
    void progress(uint64_t generation, gog::InstallState state, uint64_t done = 0, uint64_t total = 0, gog::InstallError error = gog::InstallError::none) {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        (void)generation;
        install_.state = state; install_.done = done; install_.total = total; install_.error = error; install_dirty_ = true;
    }
    void installGame(const Work& work) {
        const auto& game = *work.game;
        const auto& request = *work.request;
        const Cancel cancel = [&] { return stop_.load() || operation_cancel_.load(); };
        progress(work.generation, gog::InstallState::loading);
        const auto profile = readProfile(config_);
        const bool owned = profile.account && std::any_of(profile.games.begin(), profile.games.end(), [&](const Game& g) { return g.source_id == game.source_id; });
        if (!owned) { progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::account); return; }
        if (!validProductId(game.source_id) || !request.install_directory.is_absolute()) {
            progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::unavailable); return;
        }
        const auto exe = helper(work.generation, false, cancel);
        if (exe.empty()) { progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::installer); return; }
        std::error_code ec; std::filesystem::create_directories(request.install_directory, ec);
        if (ec) { progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::storage); return; }
        progress(work.generation, gog::InstallState::downloading);
        const auto result = operations_.invoke(exe, config_, {L"install", text_encoding::wide(game.source_id), L"--path", request.install_directory.native()}, cancel,
            [&](std::string_view line) { if (const auto bytes = progressBytes(line)) progress(work.generation, gog::InstallState::downloading, bytes->first, bytes->second); });
        if (cancel()) { progress(work.generation, gog::InstallState::cancelled); return; }
        if (!result.ok) { progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::network); return; }
        // Nile exits 0 after some failures: the installation must be recorded, in that folder, and startable.
        progress(work.generation, gog::InstallState::verifying);
        const auto after = readProfile(config_);
        const auto found = std::find_if(after.games.begin(), after.games.end(), [&](const Game& g) { return g.source_id == game.source_id; });
        const bool same_folder = found != after.games.end() && found->installed &&
            std::filesystem::equivalent(found->install_directory, request.install_directory, ec);
        progress(work.generation, same_folder ? gog::InstallState::completed : gog::InstallState::error, 0, 0,
                 same_folder ? gog::InstallError::none : gog::InstallError::detection);
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
            login_.reset();
            publishState(generation, State::preparing);
            const auto exe = helper(generation, true, cancel);
            if (cancel()) return;
            if (exe.empty()) { auto s = profileSnapshot(Failure::helper); s.state = State::error; publish(generation, std::move(s)); return; }
            // "Zmień konto": the previous device registration goes first.
            if (readProfile(config_).account) { operations_.invoke(exe, config_, {L"auth", L"--logout"}, cancel, {}); removeCredentials(); }
            if (cancel()) return;
            auto result = operations_.invoke(exe, config_, {L"auth", L"--login", L"--non-interactive"}, cancel, {});
            if (cancel()) return;
            auto login = result.ok ? parseLogin(result.output) : std::nullopt;
            SecureZeroMemory(result.output.data(), result.output.size());
            if (!login) { auto s = profileSnapshot(Failure::network); s.state = State::error; publish(generation, std::move(s)); return; }
            Snapshot s = profileSnapshot(); s.state = State::signing_in; s.browser_url = login->url;
            login_ = std::move(login);
            publish(generation, std::move(s));
            break;
        }
        case Command::complete: {
            const auto code = authorizationCode(work.input);
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t));
            if (!login_) { publish(generation, profileSnapshot()); return; }
            if (!code) { auto s = profileSnapshot(Failure::invalid); s.state = State::signing_in; publish(generation, std::move(s)); return; }
            publishState(generation, State::verifying);
            const auto exe = helper(generation, false, cancel);
            if (exe.empty()) { login_.reset(); auto s = profileSnapshot(Failure::helper); s.state = State::error; publish(generation, std::move(s)); return; }
            auto result = operations_.invoke(exe, config_, {L"register", L"--code", text_encoding::wide(*code), L"--client-id", text_encoding::wide(login_->client_id),
                L"--code-verifier", text_encoding::wide(login_->code_verifier), L"--serial", text_encoding::wide(login_->serial)}, cancel, {});
            SecureZeroMemory(result.output.data(), result.output.size());
            if (cancel()) return;
            login_.reset();
            auto s = profileSnapshot(); // `register` syncs the library itself.
            if (s.account.empty()) { s.state = State::error; s.failure = result.ok ? Failure::invalid : Failure::network; }
            publish(generation, std::move(s));
            break;
        }
        case Command::sync: {
            if (!readProfile(config_).account) { publish(generation, profileSnapshot()); return; }
            publishState(generation, State::syncing);
            const auto exe = helper(generation, false, cancel);
            if (exe.empty()) { publish(generation, profileSnapshot(Failure::helper)); return; }
            const auto result = operations_.invoke(exe, config_, {L"library", L"sync"}, cancel, {});
            if (cancel()) return;
            publish(generation, profileSnapshot(result.ok ? Failure::none : Failure::network));
            break;
        }
        case Command::cancel: login_.reset(); publish(generation, profileSnapshot()); break;
        case Command::logout: {
            login_.reset();
            if (const auto exe = helper(generation, false, cancel); !exe.empty() && readProfile(config_).account)
                operations_.invoke(exe, config_, {L"auth", L"--logout"}, [&] { return stop_.load(); }, {});
            removeCredentials(); // The device registration is gone with them even when Amazon was unreachable.
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
    NileService(std::filesystem::path config, Operations operations) : config_(std::move(config)), operations_(std::move(operations)) {
        worker_ = std::thread([this] { run(); });
    }
    ~NileService() override {
        { std::lock_guard lock(mutex_); stop_ = true; queue_.clear(); }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void submit(Command command, std::wstring input) override {
        std::lock_guard lock(mutex_);
        // A new sign-in, a cancel or a sign-out supersedes what was running (installs keep theirs).
        if (command == Command::connect || command == Command::cancel || command == Command::logout) {
            ++generation_;
            std::erase_if(queue_, [](const Work& w) { return !w.game; });
        }
        queue_.push_back({command, std::move(input), generation_.load(), {}, {}});
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
        queue_.push_back({Command::sync, {}, generation_.load(), game, std::move(request)});
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
}

std::unique_ptr<Service> makeService(std::filesystem::path config, Operations operations) {
    return std::make_unique<NileService>(std::move(config), std::move(operations));
}

bool uninstall(const std::filesystem::path& config, const Game& game, const Cancel& cancel, const Operations& operations) {
    if (game.source != GameSource::amazon || !validProductId(game.source_id)) return false;
    const auto exe = operations.prepare(config, false, cancel, [](uint64_t) {});
    if (exe.empty() || cancel()) return false;
    const auto result = operations.invoke(exe, config, {L"uninstall", text_encoding::wide(game.source_id)}, cancel, {});
    if (!result.ok) return false;
    const auto installed = installedPaths(readText(nileDirectory(config) / L"installed.json", 8 * 1024 * 1024));
    return !installed.contains(game.source_id);
}

std::wstring statusText(const Snapshot& s) {
    if (s.downloading) {
        const auto percent = s.download_total ? s.download_done * 100 / s.download_total : 0;
        return tr(L"Pobieranie Nile… ") + std::to_wstring(percent) + L"%";
    }
    switch (s.state) {
    case State::preparing: return tr(L"Przygotowanie logowania Amazon…");
    case State::signing_in: return s.failure == Failure::invalid ? tr(L"Nieprawidłowy adres. Wklej cały adres strony po logowaniu.") :
                                                                   tr(L"Zaloguj się w przeglądarce i wklej adres strony.");
    case State::verifying: return tr(L"Weryfikacja konta Amazon…");
    case State::syncing: return tr(L"Pobieranie biblioteki Amazon…");
    case State::connected: {
        auto text = tr(L"Połączono · gry: ") + std::to_wstring(s.games);
        if (s.failure == Failure::network) text += std::wstring(L" · ") + tr(L"Amazon offline");
        else if (s.failure == Failure::helper) text += std::wstring(L" · ") + tr(L"brak Nile");
        return text;
    }
    case State::error:
        switch (s.failure) {
        case Failure::helper: return tr(L"Nie można pobrać ani uruchomić Nile.");
        case Failure::invalid: return tr(L"Logowanie Amazon nie powiodło się. Spróbuj ponownie.");
        case Failure::storage: return tr(L"Nie można zapisać danych konta Amazon.");
        default: return tr(L"Brak połączenia z Amazon. Spróbuj ponownie.");
        }
    default: return tr(L"Konto niepołączone");
    }
}
}
