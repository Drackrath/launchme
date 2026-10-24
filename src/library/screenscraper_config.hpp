#pragma once

#include <array>
#include <string>
#include <string_view>

// ScreenScraper API settings (Settings → Biblioteka). Values live in %LOCALAPPDATA%\Launchme;
// an environment variable of the same field overrides the file. Passwords are DPAPI-encrypted
// (*.dat), the other fields are one-line text files. Nothing is built into the program.
namespace screenscraper {
enum class Field { dev_id, dev_password, softname, output, base_url, user, password };

struct FieldInfo {
    Field field;
    std::string_view key;          // Settings row key.
    const wchar_t* environment;    // Overrides the stored value when set.
    const wchar_t* file;           // In userConfigDirectory().
    bool secret;                   // Masked while typed, never shown, DPAPI on disk.
    const wchar_t* fallback;       // Used when nothing is stored.
    const wchar_t* title;          // tr() key.
    const wchar_t* description;    // tr() key.
};

inline constexpr std::wstring_view default_base_url = L"https://api.screenscraper.fr/api2/";

extern const std::array<FieldInfo, 7> fields;
const FieldInfo* findField(std::string_view key);

struct Config {
    std::wstring dev_id, dev_password, softname, output, base_url, user, password;
    // devid/devpassword are mandatory for every API call; ssid/sspassword are optional (quotas).
    bool ready() const { return !dev_id.empty() && !dev_password.empty() && !softname.empty() && !base_url.empty(); }
};

std::wstring value(Field field);   // Stored or environment value, else the field's fallback.
bool stored(Field field);          // A value was saved in Settings (not the fallback).
bool save(Field field, std::wstring_view text); // Empty text removes the stored value.
Config load();
// Only https:// URLs ending in '/' (a missing slash is added); anything else is rejected.
std::wstring normalizedBaseUrl(std::wstring_view text);
}
