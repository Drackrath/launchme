#include "multimc.hpp"
#include "../net/json.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <set>

namespace multimc {
namespace {
constexpr size_t max_instances = 2000;

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}
bool isFile(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_regular_file(path, ec); }
bool isFolder(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_directory(path, ec); }
std::string readFile(const std::filesystem::path& path, size_t limit = 1024 * 1024) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > static_cast<std::streamoff>(limit)) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}
std::filesystem::path environment(const wchar_t* name) {
    wchar_t value[4096]{};
    const auto count = GetEnvironmentVariableW(name, value, 4096);
    return count && count < 4096 ? std::filesystem::path(std::wstring(value, count)) : std::filesystem::path{};
}
std::wstring registryString(HKEY root, const wchar_t* path, const wchar_t* value) {
    wchar_t buffer[2048]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, path, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return buffer;
}
std::wstring trim(std::wstring_view text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::iswspace(text[begin])) ++begin;
    while (end > begin && std::iswspace(text[end - 1])) --end;
    return std::wstring(text.substr(begin, end - begin));
}
// \n \t \r \\ \" \# and QSettings' \xHHHH; any other escaped character stands for itself.
std::wstring unescape(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\' || i + 1 == text.size()) { result += text[i]; continue; }
        const wchar_t next = text[++i];
        if (next == L'n') result += L'\n';
        else if (next == L't') result += L'\t';
        else if (next == L'r') result += L'\r';
        else if (next == L'x') {
            unsigned value = 0; size_t digits = 0;
            while (digits < 4 && i + 1 < text.size() && std::iswxdigit(text[i + 1])) {
                const wchar_t ch = text[++i];
                value = value * 16 + static_cast<unsigned>(ch <= L'9' ? ch - L'0' : (std::towlower(ch) - L'a' + 10));
                ++digits;
            }
            if (digits) result += static_cast<wchar_t>(value); else result += L'x';
        } else result += next;
    }
    return result;
}
const wchar_t* configFile(Flavor flavor) {
    switch (flavor) {
    case Flavor::prism: return L"prismlauncher.cfg";
    case Flavor::polymc: return L"polymc.cfg";
    default: return L"multimc.cfg";
    }
}
// Instance folders the launchers use for their own work.
bool reservedFolder(const std::wstring& name) {
    if (name.empty() || name.front() == L'.' || name.front() == L'_') return true;
    const auto folded = lower(name);
    return folded == L"instgroups.json";
}
std::map<std::wstring, std::wstring> instanceGroups(const std::vector<std::filesystem::path>& files) {
    std::map<std::wstring, std::wstring> result;
    for (const auto& file : files) {
        const auto root = json::parse(readFile(file));
        const auto* groups = root ? root->get("groups") : nullptr;
        if (!groups || groups->type != json::Value::Type::object) continue;
        for (const auto& [name, group] : groups->properties) {
            const auto* members = group.get("instances");
            if (!members || members->type != json::Value::Type::array) continue;
            for (const auto& member : members->elements)
                if (const auto id = text_encoding::wideLenient(member.stringOr()); !id.empty())
                    result.try_emplace(id, text_encoding::wideLenient(name));
        }
        if (!result.empty()) break;
    }
    return result;
}
std::wstring minecraftVersion(const std::filesystem::path& pack) {
    const auto root = json::parse(readFile(pack));
    const auto* components = root ? root->get("components") : nullptr;
    if (!components || components->type != json::Value::Type::array) return {};
    for (const auto& component : components->elements) {
        const auto* uid = component.get("uid");
        const auto* version = component.get("version");
        if (uid && uid->stringOr() == "net.minecraft" && version) return text_encoding::wideLenient(version->stringOr()).substr(0, 40);
    }
    return {};
}
uint64_t number(std::wstring_view text) {
    uint64_t value = 0;
    if (text.empty() || text.size() > 19) return 0;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return 0;
        value = value * 10 + static_cast<uint64_t>(ch - L'0');
    }
    return value;
}
} // namespace

std::vector<std::pair<std::wstring, std::wstring>> parseConfig(std::string_view bytes) {
    std::vector<std::pair<std::wstring, std::wstring>> result;
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto text = text_encoding::wideLenient(bytes);
    size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        const auto line = trim(std::wstring_view(text).substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == L'#' || line.front() == L';' || line.front() == L'[') continue;
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos || !equals) continue;
        const auto key = trim(std::wstring_view(line).substr(0, equals));
        auto value = trim(std::wstring_view(line).substr(equals + 1));
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
        if (!key.empty() && result.size() < 4096) result.emplace_back(key, unescape(value));
        if (end == text.size()) break;
    }
    return result;
}

std::wstring configValue(const std::vector<std::pair<std::wstring, std::wstring>>& config, std::wstring_view key) {
    // The last assignment wins, as in the launchers; [General] keys carry no prefix.
    for (auto it = config.rbegin(); it != config.rend(); ++it)
        if (it->first == key || (it->first.size() == key.size() + 8 && it->first.starts_with(L"General/") && it->first.ends_with(key))) return it->second;
    return {};
}

const wchar_t* flavorName(Flavor flavor) {
    switch (flavor) {
    case Flavor::prism: return L"Prism Launcher";
    case Flavor::polymc: return L"PolyMC";
    default: return L"MultiMC";
    }
}

std::string instanceId(const std::filesystem::path& directory) {
    auto key = lower(directory.lexically_normal().wstring());
    std::replace(key.begin(), key.end(), L'/', L'\\');
    while (key.size() > 3 && key.back() == L'\\') key.pop_back();
    uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t ch : key) {
        hash ^= static_cast<uint16_t>(ch) & 0xff; hash *= 1099511628211ULL;
        hash ^= static_cast<uint16_t>(ch) >> 8; hash *= 1099511628211ULL;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) { result[static_cast<size_t>(i)] = hex[hash & 15]; hash >>= 4; }
    return result;
}

std::optional<Flavor> flavorOf(const std::filesystem::path& executable) {
    const auto name = lower(executable.filename().wstring());
    if (name == L"multimc.exe") return Flavor::multimc;
    if (name == L"prismlauncher.exe") return Flavor::prism;
    if (name == L"polymc.exe") return Flavor::polymc;
    return std::nullopt;
}

std::filesystem::path dataDirectory(const std::filesystem::path& executable, Flavor flavor) {
    const auto folder = executable.parent_path();
    if (flavor == Flavor::multimc) return folder; // Always portable.
    if (isFolder(folder / L"UserData")) return folder / L"UserData";
    std::error_code ec;
    if (std::filesystem::exists(folder / L"portable.txt", ec) || isFile(folder / configFile(flavor))) return folder;
    if (flavor == Flavor::prism)
        if (const auto chosen = environment(L"PRISMLAUNCHER_DATA_DIR"); isFolder(chosen)) return chosen;
    const auto roaming = environment(L"APPDATA");
    return roaming.empty() ? std::filesystem::path{} : roaming / (flavor == Flavor::prism ? L"PrismLauncher" : L"PolyMC");
}

std::filesystem::path instancesDirectory(const Installation& installation) {
    std::wstring folder;
    std::vector<const wchar_t*> files{configFile(installation.flavor)};
    if (installation.flavor == Flavor::prism) { files.push_back(L"polymc.cfg"); files.push_back(L"multimc.cfg"); }
    for (const auto* file : files) {
        const auto bytes = readFile(installation.data / file);
        if (bytes.empty()) continue;
        folder = configValue(parseConfig(bytes), L"InstanceDir");
        break;
    }
    if (folder.empty()) folder = L"instances";
    const std::filesystem::path path(folder);
    return path.is_absolute() ? path : installation.data / path;
}

std::optional<Instance> readInstance(const std::filesystem::path& directory) {
    const auto bytes = readFile(directory / L"instance.cfg");
    if (bytes.empty()) return std::nullopt;
    const auto config = parseConfig(bytes);
    // Prism lists only Minecraft instances ("OneSix", or no type at all).
    if (const auto type = configValue(config, L"InstanceType"); !type.empty() && type != L"OneSix") return std::nullopt;
    Instance instance;
    instance.directory = directory;
    instance.folder = directory.filename().wstring();
    instance.id = instanceId(directory);
    instance.name = trim(configValue(config, L"name")).substr(0, 200);
    if (instance.name.empty()) instance.name = instance.folder;
    instance.icon = configValue(config, L"iconKey").substr(0, 120);
    if (instance.icon.empty()) instance.icon = L"default";
    instance.last_launch = number(configValue(config, L"lastLaunchTime")) / 1000;
    instance.played_seconds = number(configValue(config, L"totalTimePlayed"));
    instance.version = minecraftVersion(directory / L"mmc-pack.json");
    return instance;
}

std::vector<Instance> instances(const Installation& installation) {
    std::vector<Instance> result;
    const auto folder = instancesDirectory(installation);
    const auto groups = instanceGroups({folder / L"instgroups.json", installation.data / L"instgroups.json"});
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end && result.size() < max_instances; it.increment(ec)) {
        std::error_code type;
        if (!it->is_directory(type) || reservedFolder(it->path().filename().wstring())) continue;
        auto instance = readInstance(it->path());
        if (!instance) continue;
        if (const auto group = groups.find(instance->folder); group != groups.end()) instance->group = group->second;
        result.push_back(std::move(*instance));
    }
    std::sort(result.begin(), result.end(), [](const Instance& a, const Instance& b) { return lower(a.name) < lower(b.name); });
    return result;
}

std::vector<Installation> discover(const std::vector<std::filesystem::path>& chosen) {
    std::vector<std::filesystem::path> candidates;
    const auto add = [&](const std::filesystem::path& path) { if (!path.empty() && path.is_absolute()) candidates.push_back(path); };
    const auto addFolder = [&](const std::filesystem::path& folder) {
        if (folder.empty() || !folder.is_absolute()) return;
        for (const auto* name : {L"prismlauncher.exe", L"MultiMC.exe", L"polymc.exe"}) add(folder / name);
    };
    for (const auto& path : chosen) {
        if (flavorOf(path)) add(path);
        else addFolder(path);
    }
    // Prism Launcher's installer (also winget's) records its folder twice.
    addFolder(registryString(HKEY_CURRENT_USER, L"Software\\PrismLauncher", L"InstallDir"));
    addFolder(registryString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PrismLauncher", L"InstallLocation"));
    addFolder(registryString(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PrismLauncher", L"InstallLocation"));
    addFolder(registryString(HKEY_CURRENT_USER, L"Software\\PolyMC", L"InstallDir"));
    const auto local = environment(L"LOCALAPPDATA"), profile = environment(L"USERPROFILE");
    const auto programs = environment(L"ProgramFiles");
    if (!local.empty()) {
        add(local / L"Programs" / L"PrismLauncher" / L"prismlauncher.exe");
        add(local / L"Programs" / L"PolyMC" / L"polymc.exe");
        add(local / L"MultiMC" / L"MultiMC.exe");
    }
    if (!programs.empty()) add(programs / L"PrismLauncher" / L"prismlauncher.exe");
    if (!profile.empty()) {
        add(profile / L"scoop" / L"apps" / L"prismlauncher" / L"current" / L"prismlauncher.exe");
        add(profile / L"scoop" / L"apps" / L"multimc" / L"current" / L"MultiMC.exe");
        // MultiMC has no installer: the folders people usually unpack it into.
        for (const auto* folder : {L"MultiMC", L"Desktop\\MultiMC", L"Documents\\MultiMC", L"Downloads\\MultiMC", L"Games\\MultiMC"})
            add(profile / folder / L"MultiMC.exe");
    }
    if (const auto drive = environment(L"SystemDrive"); !drive.empty()) {
        add(drive / L"\\" / L"MultiMC" / L"MultiMC.exe");
        add(drive / L"\\" / L"Games" / L"MultiMC" / L"MultiMC.exe");
    }
    std::vector<Installation> result;
    std::set<std::wstring> data_folders, executables;
    for (const auto& executable : candidates) {
        const auto flavor = flavorOf(executable);
        if (!flavor || !isFile(executable) || !executables.insert(lower(executable.lexically_normal().wstring())).second) continue;
        auto data = dataDirectory(executable, *flavor);
        if (!isFolder(data) || !data_folders.insert(lower(data.lexically_normal().wstring())).second) continue;
        result.push_back({*flavor, executable, std::move(data)});
        if (result.size() >= 8) break;
    }
    return result;
}

GameAction action(const Installation& installation, const Instance& instance) {
    if (installation.executable.empty() || instance.folder.empty()) return {};
    return {installation.executable.wstring(), L"--launch " + command_line::quoteArgument(instance.folder), installation.executable.parent_path(), ActionKind::play};
}

Game makeGame(const Installation& installation, const Instance& instance) {
    Game game;
    game.kind = GameKind::store;
    game.source = GameSource::multimc;
    game.source_id = instance.id;
    game.name = instance.name;
    game.installed = true;
    game.last_played = instance.last_launch;
    game.play_action = action(installation, instance);
    // The instance's folder: its Java process names it on its command line (game_activity.cpp).
    // No executable: the launcher's own EXE runs without a game too.
    game.install_directory = instance.directory;
    game.store_playtime_minutes = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, instance.played_seconds / 60));
    return game;
}

std::vector<Game> scan(const std::vector<Installation>& installations) {
    std::vector<Game> result;
    std::set<std::string> seen;
    for (const auto& installation : installations)
        for (const auto& instance : instances(installation))
            if (seen.insert(instance.id).second) result.push_back(makeGame(installation, instance));
    return result;
}
} // namespace multimc
