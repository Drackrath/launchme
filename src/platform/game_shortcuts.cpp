#include "game_shortcuts.hpp"
#include "text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <objbase.h> // CoInitializeEx (Windows.h leaves COM out under WIN32_LEAN_AND_MEAN).
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#else
#include "file_listing.hpp"
#include "process_spawn.hpp"
#include "system.hpp"
#include "../library/xdg_paths.hpp"
#include <sys/stat.h>
#endif

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace game_shortcuts {
namespace {
std::wstring hex(const std::string& bytes) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const unsigned char c : bytes) { text += digits[c >> 4]; text += digits[c & 15]; }
    return text;
}

uint32_t fnv(std::string_view text) {
    uint32_t hash = 2166136261u;
    for (const unsigned char c : text) { hash ^= c; hash *= 16777619u; }
    return hash;
}
} // namespace

std::wstring arguments(const std::string& key) { return L"--jump=launch:" + hex(key); }

std::string slug(const std::string& key) {
    std::string text;
    for (const unsigned char c : key) {
        if (std::isalnum(c)) text += static_cast<char>(std::tolower(c));
        else if (!text.empty() && text.back() != '-') text += '-';
        if (text.size() >= 60) break;
    }
    while (!text.empty() && text.back() == '-') text.pop_back();
    char suffix[12]{};
    std::snprintf(suffix, sizeof suffix, "%08x", fnv(key));
    return (text.empty() ? std::string("game") : text) + "-" + suffix;
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

namespace {
// The calling thread's COM, for the shell's link objects (the UI thread already has it).
struct Com {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Com() { if (SUCCEEDED(result)) CoUninitialize(); }
};

std::filesystem::path knownPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) path = raw;
    CoTaskMemFree(raw);
    return path;
}

// A title as a file name: no characters Windows refuses, no trailing dots or spaces.
std::wstring fileName(const std::wstring& title) {
    std::wstring name;
    for (const wchar_t c : title) name += (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos) ? L' ' : c;
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());
    if (name.size() > 120) name.resize(120);
    return name.empty() ? std::wstring(L"Game") : name;
}

// The name it gets, else the one beside it when a shortcut that is not Launchme's has the name.
std::vector<std::filesystem::path> candidates(Place place, const std::wstring& title) {
    const auto base = folder(place);
    if (base.empty()) return {};
    const auto name = fileName(title);
    return {base / (name + L".lnk"), base / (name + L" (launchme).lnk")};
}

bool ours(const std::filesystem::path& file, const std::wstring& wanted) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return false;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))) ||
        FAILED(link.As(&persist)) || FAILED(persist->Load(file.c_str(), STGM_READ))) return false;
    wchar_t buffer[INFOTIPSIZE]{};
    return SUCCEEDED(link->GetArguments(buffer, static_cast<int>(std::size(buffer)))) && wanted == buffer;
}
} // namespace

std::filesystem::path folder(Place place) { return knownPath(place == Place::desktop ? FOLDERID_Desktop : FOLDERID_Programs); }

std::filesystem::path find(Place place, const std::string& key, const std::wstring& title) {
    const Com com;
    const auto wanted = arguments(key);
    for (const auto& file : candidates(place, title)) if (ours(file, wanted)) return file;
    return {};
}

std::filesystem::path create(Place place, const Shortcut& shortcut) {
    const Com com;
    const auto wanted = arguments(shortcut.key);
    const auto names = candidates(place, shortcut.title);
    std::filesystem::path target;
    std::error_code ec;
    for (const auto& file : names) if (ours(file, wanted)) { target = file; break; }
    if (target.empty()) for (const auto& file : names) if (!std::filesystem::exists(file, ec)) { target = file; break; }
    if (target.empty() || shortcut.program.empty()) return {};
    std::filesystem::create_directories(target.parent_path(), ec);
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> persist;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))) || FAILED(link.As(&persist))) return {};
    link->SetPath(shortcut.program.c_str());
    link->SetArguments(wanted.c_str());
    link->SetWorkingDirectory(shortcut.program.parent_path().c_str());
    link->SetDescription(shortcut.title.c_str());
    link->SetIconLocation((shortcut.icon.empty() ? shortcut.program : shortcut.icon).c_str(), 0);
    if (FAILED(persist->Save(target.c_str(), TRUE))) return {};
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, target.c_str(), nullptr);
    return target;
}

bool remove(Place place, const std::string& key, const std::wstring& title) {
    const auto file = find(place, key, title);
    if (file.empty()) return true;
    if (!DeleteFileW(file.c_str())) return false;
    SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, file.c_str(), nullptr);
    return true;
}

#else

namespace {
std::string utf8(const std::wstring& text) { return text_encoding::utf8Lenient(text); }

// One .desktop file per game: launchme-<slug>.desktop.
std::filesystem::path fileOf(Place place, const std::string& key) {
    const auto base = folder(place);
    return base.empty() ? base : base / ("launchme-" + slug(key) + ".desktop");
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool ours(const std::filesystem::path& file, const std::string& key) {
    if (file.empty()) return false;
    const auto text = read(file);
    return text.find("\nX-Launchme-Game=" + utf8(hex(key)) + "\n") != std::string::npos;
}

// A value of the Desktop Entry spec: one line, its backslashes escaped.
std::string value(const std::string& text) {
    std::string result;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') result += ' ';
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    return result;
}

// An Exec argument: quoted, with ", `, $ and \ escaped, then as a value (\ doubled again); % as %%.
std::string execArgument(const std::string& text) {
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    std::string result;
    for (const char c : quoted) {
        if (c == '\\') result += "\\\\";
        else if (c == '%') result += "%%";
        else result += c;
    }
    return result;
}

std::string shellQuote(const std::string& text) {
    std::string result = "'";
    for (const char c : text) result += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return result + "'";
}

// Written beside, then moved over it: never a half-written file.
bool write(const std::filesystem::path& file, const std::string& text, mode_t mode) {
    const auto part = file.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out.flush()) return false;
    }
    ::chmod(part.c_str(), mode);
    std::error_code ec;
    std::filesystem::rename(part, file, ec);
    if (ec) std::filesystem::remove(part, ec);
    return !ec;
}
} // namespace

std::filesystem::path folder(Place place) {
    if (place == Place::desktop) return file_listing::knownFolder(file_listing::Known::desktop);
    const auto data = xdg_paths::dataHome();
    return data.empty() ? data : data / "applications";
}

std::filesystem::path find(Place place, const std::string& key, const std::wstring&) {
    const auto file = fileOf(place, key);
    return ours(file, key) ? file : std::filesystem::path{};
}

std::filesystem::path create(Place place, const Shortcut& shortcut) {
    const auto file = fileOf(place, shortcut.key);
    if (file.empty() || shortcut.program.empty()) return {};
    std::error_code ec;
    if (std::filesystem::exists(file, ec) && !ours(file, shortcut.key)) return {}; // Someone else's file: kept.
    std::filesystem::create_directories(file.parent_path(), ec);
    std::string text = "[Desktop Entry]\nType=Application\nVersion=1.0\n";
    text += "Name=" + value(utf8(shortcut.title)) + "\n";
    text += "Comment=Launchme\n";
    text += "Exec=" + execArgument(shortcut.program.string()) + " " + utf8(arguments(shortcut.key)) + "\n";
    text += "Icon=" + (shortcut.icon.empty() ? std::string("applications-games") : value(shortcut.icon.string())) + "\n";
    text += "Terminal=false\nCategories=Game;\nStartupNotify=false\n";
    text += "X-Launchme-Game=" + utf8(hex(shortcut.key)) + "\n";
    // The desktop's launchers run only when executable (and, on GNOME's desktop, trusted).
    if (!write(file, text, 0755)) return {};
    if (place == Place::desktop)
        if (const auto gio = xdg_paths::findProgram({L"gio"}); !gio.empty()) {
            pid_t pid = 0;
            if (process_spawn::spawn({gio.string(), "set", file.string(), "metadata::trusted", "true"}, {}, pid) == 0) process_spawn::reapLater(pid);
        }
    return file;
}

bool remove(Place place, const std::string& key, const std::wstring& title) {
    const auto file = find(place, key, title);
    std::error_code ec;
    return file.empty() || std::filesystem::remove(file, ec);
}

std::filesystem::path helper(const std::filesystem::path& data, const std::filesystem::path& program, bool create) {
    if (data.empty() || program.empty()) return {};
    const auto file = data / "bin" / "launchme";
    std::error_code ec;
    if (!create && !std::filesystem::exists(file, ec)) return {};
    std::string text = "#!/bin/sh\n"
        "# Launchme's shortcuts (desktop, applications menu, Steam) start it through this script;\n"
        "# Launchme rewrites it when it moves (a new AppImage), so they keep working.\n";
    if (const auto config = system_info::environment(L"LAUNCHME_CONFIG_DIR", 4095); config && !config->empty())
        text += "export LAUNCHME_CONFIG_DIR=" + shellQuote(utf8(*config)) + "\n";
    text += "exec " + shellQuote(program.string()) + " \"$@\"\n";
    if (read(file) == text) return file;
    std::filesystem::create_directories(file.parent_path(), ec);
    return write(file, text, 0755) ? file : std::filesystem::path{};
}
#endif
} // namespace game_shortcuts
