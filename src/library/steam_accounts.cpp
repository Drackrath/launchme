#include "steam_accounts.hpp"
#include "../platform/ascii_text.hpp"
#include "steam.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include "steam_registry.hpp"
#include "../platform/proc_fs.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/text_encoding.hpp"
#include <csignal>
#include <unistd.h>
#endif

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <thread>

namespace steam_accounts {
namespace {
std::string_view trim(std::string_view text) { return ascii_text::trim(text, " \t\r"); }
// The quoted strings of a VDF line ("key" or "key" "value"); escapes \\ and \" decoded.
std::vector<std::string> quoted(std::string_view line) {
    std::vector<std::string> result;
    size_t at = 0;
    while (at < line.size() && result.size() < 2) {
        const auto open = line.find('"', at);
        if (open == std::string_view::npos) break;
        std::string value;
        size_t i = open + 1;
        bool closed = false;
        for (; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size() && (line[i + 1] == '\\' || line[i + 1] == '"')) { value += line[++i]; continue; }
            if (line[i] == '"') { closed = true; break; }
            value += line[i];
        }
        if (!closed) break;
        result.push_back(std::move(value));
        at = i + 1;
    }
    return result;
}
uint64_t number(std::string_view text) {
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}
bool equalsNoCase(std::string_view a, std::string_view b) { return ascii_text::equalIgnoreCase(a, b); }
std::string escaped(std::string_view text) {
    std::string result;
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') result += '\\';
        if (static_cast<unsigned char>(ch) >= 0x20) result += ch;
    }
    return result;
}
std::optional<std::string> readText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 4 * 1024 * 1024) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    return text;
}
#ifdef _WIN32
bool startProcess(const std::filesystem::path& exe, const std::wstring& arguments) {
    if (exe.empty()) return false;
    std::wstring line = L"\"" + exe.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto directory = exe.parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}
#else
// steam.sh in its own session: Steam outlives the launcher, and is reaped if it does not.
bool startProcess(const std::filesystem::path& exe, const std::wstring& arguments) {
    if (exe.empty()) return false;
    std::vector<std::string> line{exe.string()};
    for (auto& argument : process_spawn::splitArguments(text_encoding::utf8Lenient(arguments))) line.push_back(std::move(argument));
    process_spawn::Options options;
    options.directory = exe.parent_path(); options.session = true;
    pid_t pid = 0;
    if (process_spawn::spawn(line, options, pid) != 0) return false;
    process_spawn::reapLater(pid);
    return true;
}
// Steam's client process ("steam", started by steam.sh) while it is alive.
bool steamClient(uint32_t pid) {
    const auto stat = proc_fs::stat(pid);
    return stat && !stat->ended() && proc_fs::name(pid) == "steam" && proc_fs::owner(pid) == static_cast<uint32_t>(::getuid());
}
bool ended(uint32_t pid, uint64_t start) {
    const auto stat = proc_fs::stat(pid);
    return !stat || stat->ended() || stat->start != start;
}
#endif
} // namespace

std::vector<Account> parseLoginUsers(std::string_view vdf) {
    std::vector<Account> accounts;
    int depth = 0;
    uint64_t pending = 0;
    std::optional<Account> current;
    size_t at = 0;
    while (at <= vdf.size()) {
        const auto end = vdf.find('\n', at);
        const auto line = trim(vdf.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        at = end == std::string_view::npos ? vdf.size() + 1 : end + 1;
        if (line.starts_with('{')) {
            ++depth;
            if (depth == 2 && pending) { current = Account{}; current->steam_id = pending; }
            pending = 0;
            continue;
        }
        if (line.starts_with('}')) {
            if (depth == 2 && current) { accounts.push_back(std::move(*current)); current.reset(); }
            depth = std::max(0, depth - 1);
            continue;
        }
        const auto fields = quoted(line);
        if (fields.size() == 1 && depth == 1) { const auto id = number(fields[0]); pending = id > steam_id_offset ? id : 0; }
        else if (fields.size() == 2 && depth == 2 && current) {
            const auto& key = fields[0];
            const auto& value = fields[1];
            if (equalsNoCase(key, "AccountName")) current->account_name = value;
            else if (equalsNoCase(key, "PersonaName")) current->persona = text_encoding::wideLenient(value);
            else if (equalsNoCase(key, "RememberPassword")) current->remember = value == "1";
            else if (equalsNoCase(key, "AutoLogin") || equalsNoCase(key, "MostRecent")) current->auto_login = current->auto_login || value == "1";
        }
    }
    return accounts;
}

std::vector<Account> loginUsers(const std::filesystem::path& steam_root) {
    const auto text = readText(steam_root / L"config" / L"loginusers.vdf");
    return text ? parseLoginUsers(*text) : std::vector<Account>{};
}

std::string withAutoLogin(std::string_view vdf, uint64_t steam_id) {
    const auto accounts = parseLoginUsers(vdf);
    if (std::none_of(accounts.begin(), accounts.end(), [&](const Account& account) { return account.steam_id == steam_id; })) return {};
    std::string result;
    result.reserve(vdf.size() + 64);
    int depth = 0;
    uint64_t pending = 0, current = 0;
    bool has_auto_login = false;
    std::string indent = "\t\t";
    size_t at = 0;
    while (at < vdf.size()) {
        const auto end = vdf.find('\n', at);
        const auto raw = vdf.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at + 1); // With its newline.
        at = end == std::string_view::npos ? vdf.size() : end + 1;
        const auto line = trim(raw.ends_with('\n') ? raw.substr(0, raw.size() - 1) : raw);
        if (line.starts_with('{')) {
            ++depth;
            if (depth == 2) { current = pending; has_auto_login = false; }
            pending = 0;
            result += raw;
            continue;
        }
        if (line.starts_with('}')) {
            // The target's block without an AutoLogin line gets one, as Steam writes it.
            if (depth == 2 && current == steam_id && !has_auto_login) result += indent + "\"AutoLogin\"\t\t\"1\"" + (raw.find("\r\n") != std::string_view::npos ? "\r\n" : "\n");
            if (depth == 2) current = 0;
            depth = std::max(0, depth - 1);
            result += raw;
            continue;
        }
        const auto fields = quoted(line);
        if (fields.size() == 1 && depth == 1) { const auto id = number(fields[0]); pending = id > steam_id_offset ? id : 0; }
        if (fields.size() == 2 && depth == 2 && current) {
            const auto& key = fields[0];
            const bool auto_login = equalsNoCase(key, "AutoLogin"), recent = equalsNoCase(key, "MostRecent"), allow = equalsNoCase(key, "AllowAutoLogin");
            indent = std::string(raw.substr(0, raw.find('"')));
            if (auto_login) has_auto_login = true;
            if (auto_login || recent || (allow && current == steam_id)) {
                // The value's quotes are the line's third and fourth.
                size_t seen = 0, open = std::string_view::npos, close = std::string_view::npos;
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (raw[i] == '\\') { ++i; continue; }
                    if (raw[i] != '"') continue;
                    ++seen;
                    if (seen == 3) open = i; else if (seen == 4) { close = i; break; }
                }
                if (open != std::string_view::npos && close != std::string_view::npos) {
                    result += raw.substr(0, open + 1);
                    result += current == steam_id ? "1" : "0";
                    result += raw.substr(close);
                    continue;
                }
            }
        }
        result += raw;
    }
    return result;
}

#ifndef _WIN32
// registry.vdf's ActiveProcess/pid when that is Steam; else this user's "steam" process (the
// Flatpak client records a PID of its own namespace).
uint32_t runningSteamProcess() {
    const auto recorded = steam_registry::number({"ActiveProcess"}, "pid");
    if (recorded && *recorded && *recorded <= UINT32_MAX && steamClient(static_cast<uint32_t>(*recorded))) return static_cast<uint32_t>(*recorded);
    for (const auto pid : proc_fs::processes()) if (steamClient(pid)) return pid;
    return 0;
}

bool shutdownSteam(const std::filesystem::path& steam_exe, std::chrono::milliseconds timeout) {
    const auto pid = runningSteamProcess();
    if (!pid) return true;
    const auto stat = proc_fs::stat(pid);
    if (!stat) return true;
    if (!startProcess(steam_exe, L"-shutdown")) return false;
    // Not the launcher's child: its /proc entry (with the same start time) tells when it ends.
    const auto until = std::chrono::steady_clock::now() + timeout;
    while (!ended(pid, stat->start)) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800)); // Its helpers let go of its files.
    return true;
}

bool prepareSignIn(const std::filesystem::path& steam_root, const Account& account) {
    if (account.account_name.empty() || !account.steam_id) return false;
    const auto text = readText(steam_root / L"config" / L"loginusers.vdf");
    if (!text) return false;
    const auto updated = withAutoLogin(*text, account.steam_id);
    if (updated.empty()) return false;
    if (!steam_registry::set(steam_registry::file(), {{"AutoLoginUser", account.account_name}, {"RememberPassword", "1"}})) return false;
    return updated == *text || atomic_file::write(steam_root / L"config" / L"loginusers.vdf", updated);
}
#else
uint32_t runningSteamProcess() {
    DWORD pid = 0, size = sizeof(pid);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &pid, &size) != ERROR_SUCCESS || !pid) return 0;
    // The registry can keep the PID after Steam has exited.
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return 0;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running ? pid : 0;
}

bool shutdownSteam(const std::filesystem::path& steam_exe, std::chrono::milliseconds timeout) {
    const auto pid = runningSteamProcess();
    if (!pid) return true;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return runningSteamProcess() == 0;
    const bool asked = startProcess(steam_exe, L"-shutdown");
    const bool ended = asked && WaitForSingleObject(process, static_cast<DWORD>(std::max<int64_t>(0, timeout.count()))) == WAIT_OBJECT_0;
    CloseHandle(process);
    if (ended) std::this_thread::sleep_for(std::chrono::milliseconds(800)); // Its helpers let go of its files.
    return ended;
}

bool prepareSignIn(const std::filesystem::path& steam_root, const Account& account) {
    if (account.account_name.empty() || !account.steam_id) return false;
    const auto text = readText(steam_root / L"config" / L"loginusers.vdf");
    if (!text) return false;
    const auto updated = withAutoLogin(*text, account.steam_id);
    if (updated.empty()) return false;
    const auto name = text_encoding::wideLenient(account.account_name);
    const DWORD remember = 1;
    if (RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"AutoLoginUser", REG_SZ, name.c_str(),
                        static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS ||
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"RememberPassword", REG_DWORD, &remember, sizeof(remember)) != ERROR_SUCCESS)
        return false;
    return updated == *text || atomic_file::write(steam_root / L"config" / L"loginusers.vdf", updated);
}
#endif

bool startSteam(const std::filesystem::path& steam_exe) { return startProcess(steam_exe, L"-silent"); }

std::vector<Folder> libraryFolders(const std::filesystem::path& steam_root) {
    std::vector<Folder> result;
    for (const auto& path : steamLibraryFolders(steam_root)) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) continue; // An unplugged drive.
        Folder folder{path};
#ifdef _WIN32
        ULARGE_INTEGER free{}, total{};
        if (GetDiskFreeSpaceExW(path.c_str(), &free, &total, nullptr)) { folder.free = free.QuadPart; folder.total = total.QuadPart; }
#else
        const auto space = std::filesystem::space(path, ec);
        if (!ec) { folder.free = space.available; folder.total = space.capacity; }
#endif
        result.push_back(std::move(folder));
    }
    return result;
}

std::wstring installDirectoryName(const std::wstring& name, uint32_t app_id) {
    std::wstring result;
    for (const wchar_t ch : name) {
        if (ch < 32 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos) continue;
        result += ch;
    }
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) result.pop_back();
    while (!result.empty() && result.front() == L' ') result.erase(result.begin());
    if (result.size() > 120) result.resize(120);
    while (!result.empty() && (result.back() == L' ' || result.back() == L'.')) result.pop_back();
    return result.empty() || result == L"." || result == L".." ? L"app" + std::to_wstring(app_id) : result;
}

std::string installManifest(uint32_t app_id, const std::wstring& name, const std::filesystem::path& steam_exe, uint64_t owner) {
    const auto field = [](std::string_view key, std::string_view value) { return "\t\"" + std::string(key) + "\"\t\t\"" + escaped(value) + "\"\n"; };
    const auto id = std::to_string(app_id);
    std::string text = "\"AppState\"\n{\n";
    text += field("appid", id);
    text += field("Universe", "1");
    if (!steam_exe.empty()) text += field("LauncherPath", text_encoding::utf8Lenient(steam_exe.wstring()));
    text += field("name", text_encoding::utf8Lenient(name));
    text += field("StateFlags", "1026"); // Update required, update started: Steam downloads it.
    text += field("installdir", text_encoding::utf8Lenient(installDirectoryName(name, app_id)));
    for (const auto* key : {"LastUpdated", "SizeOnDisk", "StagingSize", "buildid"}) text += field(key, "0");
    if (owner) text += field("LastOwner", std::to_string(owner));
    for (const auto* key : {"UpdateResult", "BytesToDownload", "BytesDownloaded", "BytesToStage", "BytesStaged", "TargetBuildID",
                            "AutoUpdateBehavior", "AllowOtherDownloadsWhileRunning", "ScheduledAutoUpdate"}) text += field(key, "0");
    return text + "}\n";
}

bool writeInstallManifest(const std::filesystem::path& library, uint32_t app_id, const std::wstring& name,
                          const std::filesystem::path& steam_exe, uint64_t owner) {
    if (!app_id || library.empty()) return false;
    const auto file = library / L"steamapps" / (L"appmanifest_" + std::to_wstring(app_id) + L".acf");
    std::error_code ec;
    if (std::filesystem::exists(file, ec) || ec) return false;
    return atomic_file::write(file, installManifest(app_id, name, steam_exe, owner));
}
} // namespace steam_accounts
