#include "drm_free.hpp"
#include "drm_free_internal.hpp"
#include "game_ratings.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/identified_artwork.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <Windows.h>
#include <winver.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cwctype>
#include <deque>
#include <fstream>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "version.lib")

namespace drm_free {
using namespace detail;
namespace {
constexpr size_t max_document = 2 * 1024 * 1024; // json::parse refuses anything larger.

bool plainText(std::wstring_view text) {
    for (const wchar_t ch : text) if (ch < L' ' || ch == 127) return false;
    return text.empty() || !text_encoding::utf8(text).empty();
}

bool validPath(const std::filesystem::path& path) {
    const auto& text = path.native();
    return !text.empty() && text.size() <= max_path && plainText(text) &&
           text.find_first_of(L"\"<>|*?") == std::wstring::npos && path.is_absolute();
}

bool validId(std::string_view id) {
    return id.size() == 16 && std::all_of(id.begin(), id.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

std::wstring text(const json::Value& value, const char* key, size_t limit = max_path) {
    const auto* item = value.get(key);
    if (!item || item->type != json::Value::Type::string) return {};
    auto result = text_encoding::wide(item->text);
    if (result.size() > limit || !plainText(result)) return {};
    return result;
}

uint64_t number(const json::Value& value, const char* key, uint64_t maximum = 4102444800ULL) {
    const auto* item = value.get(key);
    if (!item || item->type != json::Value::Type::number || !std::isfinite(item->numeric) || item->numeric < 0 ||
        item->numeric > static_cast<double>(maximum) || std::floor(item->numeric) != item->numeric) return 0;
    return static_cast<uint64_t>(item->numeric);
}

bool flag(const json::Value& value, const char* key) {
    const auto* item = value.get(key);
    return item && item->type == json::Value::Type::boolean && item->flag;
}

void appendString(std::string& output, std::wstring_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output += '"';
    for (const unsigned char ch : text_encoding::utf8(value)) {
        if (ch == '"' || ch == '\\') { output += '\\'; output += static_cast<char>(ch); }
        else if (ch < 0x20) { output += "\\u00"; output += hex[ch >> 4]; output += hex[ch & 15]; }
        else output += static_cast<char>(ch);
    }
    output += '"';
}

bool fileExists(const std::filesystem::path& path) {
    const DWORD attributes = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool directoryExists(const std::filesystem::path& path) {
    const DWORD attributes = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Seconds since 1601: fits a JSON number exactly, unlike raw FILETIME ticks.
uint64_t lastWriteSeconds(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER time{}; time.LowPart = data.ftLastWriteTime.dwLowDateTime; time.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return time.QuadPart / 10'000'000ULL;
}

// Visits a folder's entries (no "." / ".."); the visitor returns false to stop.
template<class Visit> bool listFolder(const std::filesystem::path& folder, Visit&& visit) {
    WIN32_FIND_DATAW data{};
    const auto pattern = folder / L"*";
    const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return false;
    do {
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        if (!visit(data)) break;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return true;
}

bool hasSuffix(std::wstring_view text, std::wstring_view suffix) { return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix; }

struct PeInfo { bool valid = false, dll = false, console = false, x64 = false; };
PeInfo peInfo(const std::filesystem::path& file) {
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    unsigned char bytes[4096]{};
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, bytes, sizeof(bytes), &read, nullptr);
    CloseHandle(handle);
    if (!ok || read < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') return {};
    const auto u16 = [&](size_t at) { return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8)); };
    const size_t pe = static_cast<size_t>(bytes[0x3C]) | (static_cast<size_t>(bytes[0x3D]) << 8) |
                      (static_cast<size_t>(bytes[0x3E]) << 16) | (static_cast<size_t>(bytes[0x3F]) << 24);
    if (pe > read || read - pe < 24 + 70) return {true}; // Unusual layout: still an executable.
    if (bytes[pe] != 'P' || bytes[pe + 1] != 'E' || bytes[pe + 2] || bytes[pe + 3]) return {};
    PeInfo info; info.valid = true;
    const uint16_t machine = u16(pe + 4), characteristics = u16(pe + 22), subsystem = u16(pe + 24 + 68);
    info.dll = (characteristics & 0x2000) != 0;
    info.console = subsystem == 3;
    info.x64 = machine == 0x8664 || machine == 0xAA64;
    return info;
}

std::wstring versionString(const std::filesystem::path& file) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, file.c_str(), &ignored);
    if (!size || size > 4 * 1024 * 1024) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, file.c_str(), 0, size, data.data())) return {};
    std::vector<std::wstring> blocks;
    struct Translation { WORD language, codepage; };
    Translation* translations = nullptr; UINT length = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) && translations)
        for (UINT i = 0; i < length / sizeof(Translation) && i < 4; ++i) {
            wchar_t block[16]{};
            swprintf_s(block, L"%04x%04x", translations[i].language, translations[i].codepage);
            blocks.push_back(block);
        }
    for (const auto* fallback : {L"040904b0", L"040904e4", L"000004b0"}) blocks.push_back(fallback);
    for (const auto* key : {L"ProductName", L"FileDescription"})
        for (const auto& block : blocks) {
            wchar_t* value = nullptr; UINT count = 0;
            const auto query = L"\\StringFileInfo\\" + block + L"\\" + key;
            if (!VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<void**>(&value), &count) || !value || !count) continue;
            auto result = trim(std::wstring_view(value, wcsnlen(value, count)), L" \t\u00a0\u2122\u00ae\u00a9");
            if (result.size() > max_name || !plainText(result)) continue;
            if (!result.empty() && !genericProduct(result)) return result;
        }
    return {};
}

uint32_t steamAppId(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    char buffer[32]{};
    input.read(buffer, sizeof(buffer) - 1);
    std::string_view value(buffer, static_cast<size_t>(input.gcount()));
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.remove_suffix(1);
    if (value.empty() || value.size() > 10 || value.find_first_not_of("0123456789") != std::string_view::npos) return 0;
    const auto id = std::stoull(std::string(value));
    return id && id <= UINT32_MAX ? static_cast<uint32_t>(id) : 0;
}

// GOG offline/Galaxy installs describe their own primary executable.
std::optional<Detection> gogInfo(const std::filesystem::path& folder, const std::vector<std::wstring>& files) {
    for (const auto& name : files) {
        std::ifstream input(folder / name, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0 || input.tellg() > 512 * 1024) continue;
        std::string bytes(static_cast<size_t>(input.tellg()), '\0'); input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) continue;
        if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
        const auto root = json::parse(bytes);
        if (!root || root->type != json::Value::Type::object) continue;
        const auto game = root->get("gameId"), parent = root->get("rootGameId");
        if (game && parent && game->stringOr() != parent->stringOr() && !parent->stringOr().empty()) continue; // A DLC's info file.
        const auto* tasks = root->get("playTasks");
        if (!tasks || tasks->type != json::Value::Type::array) continue;
        for (const auto& task : tasks->elements) {
            if (!flag(task, "isPrimary") || text(task, "type") != L"FileTask") continue;
            const auto relative = text(task, "path");
            if (relative.empty()) continue;
            const auto executable = (folder / relative).lexically_normal();
            if (!inside(executable, folder) || !fileExists(executable)) continue;
            Detection result;
            result.executable = executable;
            result.arguments = text(task, "arguments", 2000);
            if (const auto directory = text(task, "workingDir"); !directory.empty()) {
                const auto resolved = (folder / directory).lexically_normal();
                if (resolved == folder.lexically_normal() || inside(resolved, folder)) result.working_directory = resolved;
            }
            result.title = trim(text(*root, "name", max_name));
            return result;
        }
    }
    return {};
}

// Emulators and frontends: a folder holding one is not a game (Emulacja covers them).
bool emulator(std::wstring_view stem) {
    static const wchar_t* names[]{L"shadps4", L"bb_launcher", L"retroarch", L"rpcs3", L"pcsx2", L"yuzu", L"ryujinx", L"suyu", L"sudachi",
        L"citra", L"lime3ds", L"azahar", L"cemu", L"xenia", L"dolphin", L"ppsspp", L"duckstation", L"vita3k", L"melonds", L"mgba",
        L"snes9x", L"mesen", L"bsnes", L"project64", L"flycast", L"redream", L"xemu", L"mame", L"esde", L"es-de", L"emulationstation",
        L"launchbox", L"bigbox", L"playnite", L"pegasus-fe"};
    const auto name = lower(stem);
    // "pcsx2-qt", "mame64", "Playnite.DesktopApp" match; "Dolphin Olympics" does not.
    for (const std::wstring_view item : names) {
        if (!name.starts_with(item)) continue;
        if (name.size() == item.size()) return true;
        const wchar_t next = name[item.size()];
        if (!std::iswalpha(next) && next != L' ') return true;
    }
    return false;
}

bool genericChild(std::wstring_view name) {
    static const std::unordered_set<std::wstring> generic{L"bin", L"bin32", L"bin64", L"binaries", L"x64", L"x86", L"x86_64", L"win32", L"win64",
        L"windows", L"game", L"data", L"engine", L"system", L"program", L"app", L"base", L"common", L"launcher", L"lib", L"libs",
        L"runtime", L"content", L"release", L"shipping", L"client", L"dist", L"build", L"retail", L"main"};
    return generic.contains(lower(name));
}

bool rootNoise(std::wstring_view name, DWORD attributes) {
    if (name.empty() || name.front() == L'.' || name.front() == L'$') return true;
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) && (attributes & FILE_ATTRIBUTE_SYSTEM)) return true;
    static const std::unordered_set<std::wstring> noise{L"system volume information", L"recovery", L"windows", L"windowsapps", L"xboxgames",
        L"program files", L"program files (x86)", L"programdata", L"users", L"steamapps", L"steamlibrary", L"msocache", L"config.msi",
        L"perflogs", L"onedrivetemp", L"intel", L"amd", L"nvidia"};
    return noise.contains(lower(name));
}

void collectChildren(const std::filesystem::path& folder, int depth, const ScanOptions& options, std::vector<std::pair<std::filesystem::path, int>>& work) {
    listFolder(folder, [&](const WIN32_FIND_DATAW& data) {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || rootNoise(data.cFileName, data.dwFileAttributes)) return true;
        if (depth > 0 && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true; // No link loops inside containers.
        const auto child = folder / data.cFileName;
        const auto key = folderKey(child);
        if (options.known.contains(key)) return true;
        for (const auto& known : options.known)
            if ((known.size() > key.size() && known.starts_with(key) && known[key.size()] == L'\\') ||
                (key.size() > known.size() && key.starts_with(known) && key[known.size()] == L'\\')) return true;
        work.emplace_back(child, depth);
        return true;
    });
}
} // namespace

// --- Catalog file --------------------------------------------------------------

std::wstring folderKey(const std::filesystem::path& folder) {
    auto value = folder.lexically_normal().native();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    while (value.size() > 3 && value.back() == L'\\') value.pop_back();
    return lower(value);
}

std::string folderId(const std::filesystem::path& folder) {
    uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t ch : folderKey(folder)) {
        hash ^= static_cast<uint16_t>(ch) & 0xff; hash *= 1099511628211ULL;
        hash ^= static_cast<uint16_t>(ch) >> 8; hash *= 1099511628211ULL;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) { result[static_cast<size_t>(i)] = hex[hash & 15]; hash >>= 4; }
    return result;
}

bool validRoot(const std::filesystem::path& root) {
    return validPath(root) && root.has_root_name();
}

bool inside(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const auto child = folderKey(path), parent = folderKey(folder);
    if (parent.empty() || child.size() <= parent.size() || !child.starts_with(parent)) return false;
    return parent.back() == L'\\' || child[parent.size()] == L'\\';
}

const std::filesystem::path* rootOf(const Catalog& catalog, const std::filesystem::path& folder) {
    for (const auto& root : catalog.roots) if (inside(folder, root)) return &root;
    return nullptr;
}

Loaded parse(std::string_view bytes) {
    Loaded result;
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto root = bytes.size() <= max_document ? json::parse(bytes) : std::nullopt;
    const auto* version = root ? root->get("version") : nullptr;
    const auto* roots = root ? root->get("roots") : nullptr;
    const auto* games = root ? root->get("games") : nullptr;
    if (!version || version->type != json::Value::Type::number || version->numeric != 1 ||
        !roots || roots->type != json::Value::Type::array || !games || games->type != json::Value::Type::array) {
        result.readable = false;
        return result;
    }
    auto& catalog = result.catalog;
    catalog.scanned = number(*root, "scanned");
    catalog.rawg_pause = number(*root, "rawg_pause");
    std::unordered_set<std::wstring> root_keys;
    for (const auto& item : roots->elements) {
        if (item.type != json::Value::Type::string || catalog.roots.size() >= max_roots) continue;
        const std::filesystem::path path = text_encoding::wide(item.text);
        if (!validRoot(path) || !root_keys.insert(folderKey(path)).second) continue;
        if (std::any_of(catalog.roots.begin(), catalog.roots.end(), [&](const auto& other) { return inside(path, other) || inside(other, path); })) continue;
        catalog.roots.push_back(path);
    }
    std::unordered_set<std::string> ids;
    for (const auto& item : games->elements) {
        if (item.type != json::Value::Type::object || catalog.games.size() >= max_games) continue;
        Entry entry;
        entry.id = item.get("id") ? item.get("id")->stringOr() : std::string{};
        entry.folder = text(item, "folder");
        entry.executable = text(item, "exe");
        entry.arguments = text(item, "args", 2000);
        entry.working_directory = text(item, "cwd");
        entry.title = trim(text(item, "title", max_name));
        entry.product = trim(text(item, "product", max_name));
        entry.name = trim(text(item, "name", max_name));
        entry.year = static_cast<int>(number(item, "year", 2200));
        entry.steam_app_id = static_cast<uint32_t>(number(item, "steam", UINT32_MAX));
        entry.rawg_id = static_cast<uint32_t>(number(item, "rawg", UINT32_MAX));
        entry.checked = number(item, "checked");
        entry.found = number(item, "found");
        entry.folder_time = number(item, "time", 1ULL << 52);
        entry.manual_executable = flag(item, "manual_exe");
        entry.manual_identity = flag(item, "manual_name");
        entry.ignored = flag(item, "ignored");
        entry.missing = flag(item, "missing");
        if (!validId(entry.id) || !validPath(entry.folder) || entry.id != folderId(entry.folder) || !rootOf(catalog, entry.folder) ||
            (!entry.executable.empty() && !validPath(entry.executable)) ||
            (!entry.working_directory.empty() && !validPath(entry.working_directory)) || !ids.insert(entry.id).second) continue;
        if (entry.title.empty()) entry.title = cleanTitle(entry.folder.filename().native()).text;
        if (entry.name.empty()) entry.name = entry.title;
        if (entry.name.empty()) continue;
        catalog.games.push_back(std::move(entry));
    }
    return result;
}

std::string format(const Catalog& catalog) {
    std::string output = "{\n  \"version\": 1,\n  \"scanned\": " + std::to_string(catalog.scanned) +
                         ",\n  \"rawg_pause\": " + std::to_string(catalog.rawg_pause) + ",\n  \"roots\": [";
    for (size_t i = 0; i < catalog.roots.size(); ++i) {
        output += i ? ", " : "";
        appendString(output, catalog.roots[i].native());
    }
    output += "],\n  \"games\": [";
    for (size_t i = 0; i < catalog.games.size(); ++i) {
        const auto& entry = catalog.games[i];
        output += i ? ",\n    {" : "\n    {";
        output += "\"id\": \"" + entry.id + "\"";
        output += ", \"folder\": "; appendString(output, entry.folder.native());
        output += ", \"exe\": "; appendString(output, entry.executable.native());
        if (!entry.arguments.empty()) { output += ", \"args\": "; appendString(output, entry.arguments); }
        if (!entry.working_directory.empty()) { output += ", \"cwd\": "; appendString(output, entry.working_directory.native()); }
        output += ", \"title\": "; appendString(output, entry.title);
        if (!entry.product.empty()) { output += ", \"product\": "; appendString(output, entry.product); }
        output += ", \"name\": "; appendString(output, entry.name);
        if (entry.year) output += ", \"year\": " + std::to_string(entry.year);
        if (entry.steam_app_id) output += ", \"steam\": " + std::to_string(entry.steam_app_id);
        if (entry.rawg_id) output += ", \"rawg\": " + std::to_string(entry.rawg_id);
        output += ", \"checked\": " + std::to_string(entry.checked) + ", \"found\": " + std::to_string(entry.found) +
                  ", \"time\": " + std::to_string(entry.folder_time);
        if (entry.manual_executable) output += ", \"manual_exe\": true";
        if (entry.manual_identity) output += ", \"manual_name\": true";
        if (entry.ignored) output += ", \"ignored\": true";
        if (entry.missing) output += ", \"missing\": true";
        output += '}';
    }
    output += catalog.games.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return output;
}

Loaded load(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return {};
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto size = input ? static_cast<long long>(input.tellg()) : -1;
    std::string bytes;
    if (size > 0 && static_cast<size_t>(size) <= max_document) {
        bytes.resize(static_cast<size_t>(size));
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) bytes.clear();
    }
    input.close();
    auto result = parse(bytes);
    if (!result.readable) {
        auto backup = file; backup += L".bak";
        std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
    }
    return result;
}

bool save(const Catalog& catalog, const std::filesystem::path& file) {
    if (catalog.roots.size() > max_roots || catalog.games.size() > max_games) return false;
    const auto value = format(catalog);
    return value.size() <= max_document && atomic_file::write(file, value);
}

// --- Executable detection ------------------------------------------------------

bool excludedExecutable(std::wstring_view file_name) {
    auto stem = lower(file_name);
    if (hasSuffix(stem, L".exe")) stem.resize(stem.size() - 4);
    static const wchar_t* prefixes[]{L"unins", L"uninst", L"setup", L"install", L"vcredist", L"vc_redist", L"dxsetup", L"dxwebsetup",
        L"dotnet", L"ndp4", L"oalinst", L"physx", L"ue4prereq", L"ueprereq", L"unitycrashhandler", L"crashreport", L"crashpad", L"crashhandler",
        L"crashsender", L"bugsplat", L"easyanticheat", L"beservice", L"battleye", L"cefsharp", L"qtwebengine", L"notification_helper",
        L"steamcmd", L"steamwebhelper", L"steamerrorreporter", L"gogalaxy", L"galaxyclient", L"python", L"pythonw", L"java", L"javaw",
        L"7z", L"unrar", L"winrar", L"zsync", L"dxdiag", L"touchup", L"redist", L"directx", L"xnafx", L"msiexec", L"cleanup",
        L"epicwebhelper", L"unrealcefsubprocess", L"vulkaninfo", L"dosbox", L"scummvm", L"sentry", L"elevate", L"report"};
    for (const auto* prefix : prefixes) if (stem.starts_with(prefix)) return true;
    static const wchar_t* fragments[]{L"uninstall", L"installer", L"crashreport", L"crash_report", L"crashhandler", L"redist", L"prereq",
        L"subprocess", L"webhelper", L"updater", L"patcher", L"vc_redist", L"_setup", L"-setup"};
    for (const auto* fragment : fragments) if (stem.find(fragment) != std::wstring::npos) return true;
    return stem.empty() || stem == L"steam" || stem == L"helper" || stem == L"cmd" || stem == L"config";
}

bool skippedFolder(std::wstring_view folder_name) {
    const auto name = lower(folder_name);
    if (name.empty() || name.front() == L'.' || name.front() == L'$' || name.starts_with(L"__")) return true;
    static const std::unordered_set<std::wstring> skipped{L"_commonredist", L"commonredist", L"redist", L"redistributable",
        L"redistributables", L"_redist", L"directx", L"dxsetup", L"vcredist", L"vc_redist", L"support", L"installer", L"installers",
        L"_installer", L"prerequisites", L"prereq", L"prereqs", L"dotnet", L"dotnetfx", L"netfx", L"physx", L"easyanticheat", L"eac",
        L"battleye", L"monobleedingedge", L"mono", L"crashpad", L"crashreporter", L"jre", L"jdk", L"java", L"python", L"docs",
        L"documentation", L"manual", L"manuals", L"soundtrack", L"ost", L"music", L"extras", L"bonus", L"artbook", L"goodies",
        L"screenshots", L"saves", L"savegames", L"logs", L"cache", L"shadercache", L"temp", L"tmp", L"tools", L"sdk", L"editor",
        L"mods", L"workshop", L"uninstall", L"engine", L"dosbox", L"scummvm", L"redistributable packages", L"third party",
        L"thirdparty", L"licenses", L"legal", L"cef", L"webview", L"locales", L"localization"};
    return skipped.contains(name);
}

int executableScore(const Candidate& candidate, std::wstring_view folder_name, std::wstring_view title) {
    auto stem = lower(candidate.path.stem().native());
    const auto name = ratings::normalizedTitle(stem), folder = ratings::normalizedTitle(folder_name), cleaned = ratings::normalizedTitle(title);
    int score = 100 - 25 * candidate.depth;
    if (!name.empty() && (name == folder || name == cleaned)) score += 80;
    else if ((name.size() >= 4 && (folder.find(name) != std::wstring::npos || cleaned.find(name) != std::wstring::npos)) ||
             (folder.size() >= 4 && name.find(folder) != std::wstring::npos) || (cleaned.size() >= 4 && name.find(cleaned) != std::wstring::npos))
        score += 45;
    else {
        // Initials: "Grand Theft Auto V" → "gtav".
        std::wstring initials;
        for (const auto& word : words(title.empty() ? folder_name : title)) initials += std::iswdigit(word[0]) ? word : word.substr(0, 1);
        if (initials.size() >= 2 && name == initials) score += 35;
    }
    if (candidate.unity_data) score += 120;
    if (candidate.godot_pack) score += 100;
    if (candidate.unreal_stub) score += 90;
    if (candidate.rpgmaker || candidate.renpy) score += 80;
    if (candidate.gamemaker_data) score += 60;
    if (hasSuffix(stem, L"-win64-shipping") || hasSuffix(stem, L"-win32-shipping") || hasSuffix(stem, L"-wingdk-shipping")) score += 30;
    if (candidate.size < 64 * 1024) score -= 60;
    else if (candidate.size < 300 * 1024) score -= 15;
    else if (candidate.size >= 20 * 1024 * 1024) score += 20;
    else if (candidate.size >= 2 * 1024 * 1024) score += 10;
    if (candidate.console) score -= 40;
    if (candidate.x64) score += 5;
    const auto parent = lower(candidate.path.parent_path().filename().native());
    if (parent == L"x64" || parent == L"win64" || stem.find(L"64") != std::wstring::npos) score += 8;
    if (stem.find(L"launcher") != std::wstring::npos) score -= 15;
    for (const auto* weak : {L"config", L"setting", L"option", L"editor", L"tool", L"sdk", L"server", L"dedicated", L"benchmark",
                             L"test", L"debug", L"helper", L"crash", L"register", L"activation", L"language", L"setup"})
        if (stem.find(weak) != std::wstring::npos) { score -= 60; break; }
    return score;
}

Detection detect(const std::filesystem::path& folder, const Cancelled& cancelled) {
    Detection result;
    const auto stop = [&] { return cancelled && cancelled(); };
    struct Pending { std::filesystem::path folder; int depth; };
    std::deque<Pending> queue{{folder, 0}};
    std::vector<Candidate> candidates;
    size_t visited = 0;
    constexpr size_t visit_limit = 6000;
    while (!queue.empty() && visited < visit_limit && candidates.size() < 96) {
        if (stop()) return {};
        const auto current = queue.front(); queue.pop_front();
        std::unordered_set<std::wstring> names;
        std::vector<std::pair<std::wstring, uint64_t>> executables;
        std::vector<std::wstring> folders, gog_files;
        listFolder(current.folder, [&](const WIN32_FIND_DATAW& data) {
            ++visited;
            const std::wstring name(data.cFileName);
            const auto key = lower(name);
            names.insert(key);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) folders.push_back(name);
            } else if (hasSuffix(key, L".exe")) {
                executables.emplace_back(name, (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
            } else if (current.depth == 0 && key.starts_with(L"goggame-") && hasSuffix(key, L".info")) gog_files.push_back(name);
            return visited < visit_limit;
        });
        if (current.depth == 0) {
            if (names.contains(L".egstore") || names.contains(L"microsoftgame.config") || names.contains(L"appxmanifest.xml")) {
                result.store_owned = true;
                return result;
            }
            if (names.contains(L"steam_appid.txt")) result.steam_app_id = steamAppId(current.folder / L"steam_appid.txt");
            if (!gog_files.empty()) {
                std::sort(gog_files.begin(), gog_files.end());
                if (auto gog = gogInfo(current.folder, gog_files)) {
                    gog->steam_app_id = result.steam_app_id;
                    gog->product = versionString(gog->executable);
                    if (ratings::normalizedTitle(gog->product) == ratings::normalizedTitle(gog->title)) gog->product.clear();
                    return *gog;
                }
            }
        }
        for (const auto& [name, size] : executables) {
            if (emulator(std::filesystem::path(name).stem().native())) return {}; // An emulator's folder, not a game.
            if (excludedExecutable(name)) continue;
            Candidate candidate;
            candidate.path = current.folder / name;
            candidate.depth = current.depth;
            candidate.size = size;
            const auto stem = lower(std::filesystem::path(name).stem().native());
            candidate.unity_data = names.contains(stem + L"_data");
            candidate.godot_pack = names.contains(stem + L".pck");
            candidate.gamemaker_data = names.contains(L"data.win");
            candidate.renpy = names.contains(L"renpy") && (names.contains(stem + L".py") || names.contains(L"lib"));
            candidate.rpgmaker = stem == L"game" && (names.contains(L"www") || names.contains(L"game.ini") ||
                std::any_of(names.begin(), names.end(), [](const std::wstring& item) {
                    return hasSuffix(item, L".rgss3a") || hasSuffix(item, L".rgss2a") || hasSuffix(item, L".rgssad");
                }));
            candidate.unreal_stub = names.contains(stem) && directoryExists(current.folder / stem / L"Binaries");
            const auto pe = peInfo(candidate.path);
            if (!pe.valid || pe.dll) continue;
            candidate.console = pe.console;
            candidate.x64 = pe.x64;
            candidates.push_back(std::move(candidate));
        }
        if (current.depth < 3)
            for (const auto& name : folders) if (!skippedFolder(name)) queue.push_back({current.folder / name, current.depth + 1});
    }
    if (candidates.empty()) return result;
    // No executable of its own, but games in several subfolders: a folder of games.
    if (std::none_of(candidates.begin(), candidates.end(), [](const Candidate& c) { return c.depth == 0; })) {
        std::unordered_set<std::wstring> children;
        for (const auto& candidate : candidates) {
            if (candidate.depth > 2) continue;
            const auto relative = candidate.path.lexically_relative(folder);
            if (relative.empty()) continue;
            const auto child = relative.begin()->native();
            if (!genericChild(child)) children.insert(lower(child));
        }
        if (children.size() >= 2) { result.container = true; return result; }
    }
    const auto folder_name = folder.filename().native();
    const auto title = cleanTitle(folder_name).text;
    const Candidate* best = nullptr;
    int best_score = INT_MIN;
    for (const auto& candidate : candidates) {
        const int score = executableScore(candidate, folder_name, title);
        if (!best || score > best_score || (score == best_score && (candidate.depth < best->depth ||
            (candidate.depth == best->depth && (candidate.size > best->size || (candidate.size == best->size && candidate.path < best->path)))))) {
            best = &candidate; best_score = score;
        }
    }
    result.executable = best->path;
    result.product = versionString(best->path);
    if (result.product.empty()) {
        // "RCG\RiverCityGirls.exe": the file name is the better second query.
        const auto stem = cleanTitle(best->path.stem().native()).text;
        if (stem.size() >= 4 && !genericProduct(stem) && ratings::normalizedTitle(stem) != ratings::normalizedTitle(title)) result.product = stem;
    }
    return result;
}

// --- Scanning --------------------------------------------------------------------

Catalog scan(const Catalog& previous, const ScanOptions& options, const Cancelled& cancelled, const std::function<void(Progress)>& progress) {
    const auto stop = [&] { return cancelled && cancelled(); };
    Catalog next;
    next.roots = previous.roots;
    next.scanned = previous.scanned;
    next.rawg_pause = previous.rawg_pause;
    std::unordered_map<std::string, const Entry*> before;
    for (const auto& entry : previous.games) before.emplace(entry.id, &entry);
    std::unordered_set<std::string> placed;
    std::unordered_set<std::wstring> examined; // Folder keys whose result is final for this scan.
    std::vector<std::pair<std::filesystem::path, int>> work;
    for (const auto& root : previous.roots) {
        if (stop()) break;
        if (!directoryExists(root)) {
            // An unplugged drive or offline share: its games wait, unplayable.
            for (const auto& entry : previous.games)
                if (inside(entry.folder, root) && placed.insert(entry.id).second) { auto kept = entry; kept.missing = true; next.games.push_back(std::move(kept)); }
            continue;
        }
        examined.insert(folderKey(root));
        collectChildren(root, 0, options, work);
    }
    Progress state{0, work.size()};
    if (progress) progress(state);
    bool interrupted = stop();
    for (size_t index = 0; index < work.size() && !interrupted; ++index) {
        if (stop()) { interrupted = true; break; }
        const auto folder = work[index].first;
        const int depth = work[index].second;
        const auto key = folderKey(folder);
        const auto id = folderId(folder);
        if (placed.contains(id)) { ++state.done; continue; }
        const auto found = before.find(id);
        const Entry* old = found == before.end() ? nullptr : found->second;
        const auto folder_time = lastWriteSeconds(folder);
        const auto keep = [&](Entry entry) {
            entry.missing = entry.executable.empty() || !fileExists(entry.executable);
            placed.insert(entry.id); next.games.push_back(std::move(entry));
        };
        if (old && old->manual_executable) { keep(*old); examined.insert(key); ++state.done; if (progress) progress(state); continue; }
        if (old && !options.full && old->folder_time == folder_time && (old->ignored || fileExists(old->executable))) {
            keep(*old); examined.insert(key); ++state.done; if (progress) progress(state); continue;
        }
        auto detection = detect(folder, cancelled);
        if (stop()) { interrupted = true; break; }
        examined.insert(key);
        ++state.done;
        if (detection.container && depth == 0) {
            const auto before_size = work.size();
            collectChildren(folder, 1, options, work);
            state.total += work.size() - before_size;
        } else if (!detection.store_owned && (!detection.executable.empty() || (old && old->ignored))) {
            Entry entry = old ? *old : Entry{};
            const auto cleaned = cleanTitle(folder.filename().native());
            const auto title = detection.title.empty() ? cleaned.text : detection.title;
            entry.id = id;
            entry.folder = folder;
            if (!detection.executable.empty()) {
                entry.executable = detection.executable;
                entry.arguments = detection.arguments;
                entry.working_directory = detection.working_directory;
            }
            if (!old) { entry.found = options.now; entry.name = title; }
            else if (old->title != title && !old->manual_identity && !old->rawg_id) { entry.name = title; entry.checked = 0; }
            entry.title = title;
            entry.year = cleaned.year;
            if (!detection.product.empty()) entry.product = detection.product;
            if (detection.steam_app_id) entry.steam_app_id = detection.steam_app_id;
            entry.folder_time = folder_time;
            entry.missing = entry.executable.empty();
            placed.insert(entry.id);
            next.games.push_back(std::move(entry));
        }
        if (progress) progress(state);
        if (options.gentle) std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    if (interrupted) {
        // Folders not reached keep their previous result: a partial scan is a complete catalog.
        for (const auto& entry : previous.games)
            if (!placed.contains(entry.id) && !examined.contains(folderKey(entry.folder)) && rootOf(next, entry.folder)) {
                placed.insert(entry.id); next.games.push_back(entry);
            }
    } else next.scanned = options.now;
    std::sort(next.games.begin(), next.games.end(), [](const Entry& a, const Entry& b) { return folderKey(a.folder) < folderKey(b.folder); });
    if (next.games.size() > max_games) next.games.resize(max_games);
    return next;
}

Catalog merge(const Catalog& current, Catalog scanned) {
    Catalog result;
    result.roots = current.roots;
    result.scanned = std::max(current.scanned, scanned.scanned);
    result.rawg_pause = std::max(current.rawg_pause, scanned.rawg_pause);
    std::unordered_map<std::string, const Entry*> mine;
    for (const auto& entry : current.games) mine.emplace(entry.id, &entry);
    std::unordered_set<std::string> placed;
    for (auto& entry : scanned.games) {
        if (!rootOf(current, entry.folder) || !placed.insert(entry.id).second) continue;
        if (const auto found = mine.find(entry.id); found != mine.end()) {
            const auto& user = *found->second;
            entry.ignored = user.ignored;
            if (user.manual_executable || entry.manual_executable) {
                entry.manual_executable = user.manual_executable;
                entry.executable = user.executable;
                entry.arguments = user.arguments;
                entry.working_directory = user.working_directory;
                entry.missing = user.missing;
                if (!user.manual_executable) entry.folder_time = 0; // Back to automatic meanwhile: detect again.
            }
            if (user.manual_identity) {
                entry.manual_identity = true;
                entry.name = user.name;
                entry.rawg_id = user.rawg_id;
                entry.steam_app_id = user.steam_app_id;
                entry.checked = user.checked;
            }
        }
        result.games.push_back(std::move(entry));
    }
    // Roots added after the scan's snapshot keep what they already have.
    for (const auto& entry : current.games) {
        const auto* root = rootOf(current, entry.folder);
        if (!root || placed.contains(entry.id)) continue;
        const bool scanned_root = std::any_of(scanned.roots.begin(), scanned.roots.end(), [&](const auto& other) { return folderKey(other) == folderKey(*root); });
        if (!scanned_root) { placed.insert(entry.id); result.games.push_back(entry); }
    }
    std::sort(result.games.begin(), result.games.end(), [](const Entry& a, const Entry& b) { return folderKey(a.folder) < folderKey(b.folder); });
    return result;
}

// --- Library ----------------------------------------------------------------------

std::filesystem::path workingDirectory(const Entry& entry) {
    return entry.working_directory.empty() ? entry.executable.parent_path() : entry.working_directory;
}

GameAction action(const Entry& entry) {
    if (entry.executable.empty()) return {};
    return {entry.executable.native(), entry.arguments, workingDirectory(entry), ActionKind::play};
}

Game makeGame(const Entry& entry) {
    Game game;
    game.kind = GameKind::store;
    game.source = GameSource::drmfree;
    game.source_id = entry.id;
    game.name = !entry.name.empty() ? entry.name : !entry.title.empty() ? entry.title : entry.folder.filename().native();
    game.installed = !entry.missing && !entry.executable.empty();
    game.play_action = action(entry);
    // The game owns its folder: processes started from anywhere inside it count as playing.
    game.install_directory = entry.folder;
    game.executable = entry.executable;
    return game;
}
} // namespace drm_free
