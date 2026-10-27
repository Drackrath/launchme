#include "screenscraper_config.hpp"
#include "../platform/system.hpp"
#include "../i18n/i18n.hpp"
#include "../platform/protected_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <cwctype>

namespace screenscraper {
const std::array<FieldInfo, 7> fields{{
    {Field::dev_id, "dev_id", L"SCREENSCRAPER_DEV_ID", L"screenscraper_dev_id.txt", false, L"",
     trKey(L"devid"), trKey(L"Identyfikator dewelopera z forum ScreenScraper. Wymagany.")},
    {Field::dev_password, "dev_password", L"SCREENSCRAPER_DEV_PASSWORD", L"screenscraper_dev_password.dat", true, L"",
     trKey(L"devpassword"), trKey(L"Hasło dewelopera z forum ScreenScraper. Wymagane.")},
    {Field::softname, "softname", L"SCREENSCRAPER_SOFTNAME", L"screenscraper_softname.txt", false, L"Launchme",
     trKey(L"softname"), trKey(L"Nazwa programu przypisana do devid.")},
    {Field::output, "output", L"SCREENSCRAPER_OUTPUT", L"screenscraper_output.txt", false, L"xml",
     trKey(L"output"), trKey(L"Format odpowiedzi: json albo xml.")},
    {Field::base_url, "base_url", L"SCREENSCRAPER_BASE_URL", L"screenscraper_base_url.txt", false, default_base_url.data(),
     trKey(L"Adres API"), trKey(L"Adres bazowy API (https).")},
    {Field::user, "user", L"SCREENSCRAPER_SSID", L"screenscraper_ssid.txt", false, L"",
     trKey(L"ssid (login)"), trKey(L"Twój login ScreenScraper. Opcjonalny: wyższe limity zapytań.")},
    {Field::password, "password", L"SCREENSCRAPER_SSPASSWORD", L"screenscraper_sspassword.dat", true, L"",
     trKey(L"sspassword (hasło)"), trKey(L"Hasło konta ScreenScraper. Opcjonalne.")},
}};

namespace {
const FieldInfo& info(Field field) {
    return *std::find_if(fields.begin(), fields.end(), [&](const FieldInfo& item) { return item.field == field; });
}
std::wstring environment(const wchar_t* name) {
    return system_info::environment(name, 1024).value_or(std::wstring{});
}
// The environment first, then the file (one text line, or DPAPI for passwords).
std::wstring storedText(const FieldInfo& item) {
    if (!item.secret) return userSecret(item.environment, item.file);
    if (auto text = environment(item.environment); !text.empty()) return text;
    auto plain = protected_file::read(userConfigDirectory() / item.file, 4096);
    if (!plain) return {};
    auto text = text_encoding::wide(*plain);
    SecureZeroMemory(plain->data(), plain->size());
    return text;
}
bool clean(std::wstring_view text) {
    return text.size() <= 1024 && std::none_of(text.begin(), text.end(), [](wchar_t ch) { return ch < 32 || ch == 127; });
}
}

const FieldInfo* findField(std::string_view key) {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const FieldInfo& item) { return item.key == key; });
    return found == fields.end() ? nullptr : &*found;
}

std::wstring normalizedBaseUrl(std::wstring_view text) {
    std::wstring url(text);
    const auto first = url.find_first_not_of(L" \t"), last = url.find_last_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    url = url.substr(first, last - first + 1);
    if (!url.starts_with(L"https://") || url.size() <= 8 || url.size() > 512 ||
        url.find_first_of(L" \t\r\n?#\"<>\\") != std::wstring::npos) return {};
    if (url.back() != L'/') url += L'/';
    return url;
}

std::wstring value(Field field) {
    const auto& item = info(field);
    auto text = storedText(item);
    if (field == Field::base_url) text = normalizedBaseUrl(text);
    if (field == Field::output) {
        for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
        if (text != L"json" && text != L"xml") text.clear();
    }
    return text.empty() ? std::wstring(item.fallback) : text;
}

bool stored(Field field) {
    const auto& item = info(field);
    std::error_code ec;
    return std::filesystem::exists(userConfigDirectory() / item.file, ec);
}

bool save(Field field, std::wstring_view text) {
    const auto& item = info(field);
    std::wstring trimmed(text);
    if (!item.secret) {
        const auto first = trimmed.find_first_not_of(L" \t"), last = trimmed.find_last_not_of(L" \t");
        trimmed = first == std::wstring::npos ? std::wstring{} : trimmed.substr(first, last - first + 1);
    }
    if (!clean(trimmed)) return false;
    if (field == Field::base_url && !trimmed.empty()) {
        trimmed = normalizedBaseUrl(trimmed);
        if (trimmed.empty()) return false;
    }
    if (field == Field::output && !trimmed.empty()) {
        for (auto& ch : trimmed) ch = static_cast<wchar_t>(towlower(ch));
        if (trimmed != L"json" && trimmed != L"xml") return false;
    }
    if (!item.secret) return saveUserSecret(item.file, trimmed);
    const auto file = userConfigDirectory() / item.file;
    if (trimmed.empty()) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return !ec;
    }
    auto plain = text_encoding::utf8(trimmed);
    if (plain.empty()) return false;
    const bool saved = protected_file::write(file, plain, L"Launchme ScreenScraper");
    SecureZeroMemory(plain.data(), plain.size());
    SecureZeroMemory(trimmed.data(), trimmed.size() * sizeof(wchar_t));
    return saved;
}

Config load() {
    return {value(Field::dev_id), value(Field::dev_password), value(Field::softname), value(Field::output),
            value(Field::base_url), value(Field::user), value(Field::password)};
}
}
