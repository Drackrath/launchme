// The kernel32/advapi32 calls the launcher's UI code makes, on POSIX and ICU: errors, threads,
// the environment, time and its formatting, text case and comparison, files and movable memory.
#include "window_internal.hpp"
#include "../../platform/system.hpp"
#include "../../platform/text_encoding.hpp"
#include "../../platform/unicode_text.hpp"

#include <unicode/locid.h>
#include <unicode/udat.h>
#include <unicode/ustring.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
thread_local DWORD last_error = 0;
constexpr int64_t epoch_difference = 116444736000000000LL; // 1601-01-01 to 1970-01-01 in 100 ns.

int64_t fileTimeValue(const FILETIME& time) { return (static_cast<int64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
FILETIME fileTime(int64_t value) { return {static_cast<DWORD>(value & 0xFFFFFFFF), static_cast<DWORD>(value >> 32)}; }

SYSTEMTIME systemTime(const std::tm& parts, int milliseconds) {
    return {static_cast<WORD>(parts.tm_year + 1900), static_cast<WORD>(parts.tm_mon + 1), static_cast<WORD>(parts.tm_wday),
            static_cast<WORD>(parts.tm_mday), static_cast<WORD>(parts.tm_hour), static_cast<WORD>(parts.tm_min),
            static_cast<WORD>(parts.tm_sec), static_cast<WORD>(milliseconds)};
}

int copyOut(const std::wstring& text, wchar_t* buffer, int size) {
    const int needed = static_cast<int>(text.size()) + 1;
    if (!buffer || size <= 0) return needed;
    if (size < needed) { last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::wmemcpy(buffer, text.c_str(), text.size() + 1);
    return needed;
}

std::u16string utf16(const std::wstring& text) {
    std::u16string result;
    for (const wchar_t c : text) {
        const auto code = static_cast<char32_t>(c);
        if (code > 0xFFFF) { result.push_back(static_cast<char16_t>(0xD800 + ((code - 0x10000) >> 10))); result.push_back(static_cast<char16_t>(0xDC00 + ((code - 0x10000) & 0x3FF))); }
        else result.push_back(static_cast<char16_t>(code));
    }
    return result;
}
std::wstring wide(const UChar* text, int32_t length) {
    std::wstring result;
    for (int32_t i = 0; i < length;) { UChar32 code; U16_NEXT(text, i, length, code); result.push_back(static_cast<wchar_t>(code)); }
    return result;
}

// A Windows date/time picture as an ICU pattern. Most letters agree (d, dd, M..MMMM, y, H, h, m, s,
// quoted text); they differ in the weekday (Windows ddd/dddd = ICU EEE/EEEE: ICU's dddd is a
// zero-padded day of the month), AM/PM (t/tt = a) and the era (g/gg = G).
std::wstring icuPattern(const wchar_t* pattern) {
    std::wstring result;
    for (const wchar_t* p = pattern; *p;) {
        if (*p == L'\'') { // Quoted text, '' inside it included, passes through.
            const wchar_t* end = p + 1;
            while (*end && !(*end == L'\'' && end[1] != L'\'')) end += *end == L'\'' ? 2 : 1;
            result.append(p, *end ? end + 1 : end);
            p = *end ? end + 1 : end;
            continue;
        }
        size_t run = 1;
        while (p[run] == *p) ++run;
        if (*p == L'd' && run >= 3) result.append(run >= 4 ? L"EEEE" : L"EEE");
        else if (*p == L't') result.append(L"a");
        else if (*p == L'g') result.append(L"G");
        else result.append(p, run);
        p += run;
    }
    return result;
}

// An ICU locale ID for a BCP-47 name ("pl-PL"), or nullptr (ICU's default: LANG/LC_TIME).
std::string icuLocale(const wchar_t* name) {
    if (!name || !*name) return {};
    std::string result = text_encoding::utf8Lenient(name);
    for (auto& c : result) if (c == '-') c = '_';
    return result;
}

// A date/time formatted for `locale` (the launcher's language) or the user's locale.
std::wstring formatDate(const SYSTEMTIME& time, UDateFormatStyle date_style, UDateFormatStyle time_style, const wchar_t* pattern,
                        const wchar_t* locale = nullptr) {
    std::tm parts{};
    parts.tm_year = time.wYear - 1900; parts.tm_mon = time.wMonth - 1; parts.tm_mday = time.wDay;
    parts.tm_hour = time.wHour; parts.tm_min = time.wMinute; parts.tm_sec = time.wSecond; parts.tm_isdst = -1;
    const UDate date = static_cast<UDate>(std::mktime(&parts)) * 1000.0;
    UErrorCode status = U_ZERO_ERROR;
    UDateFormat* format = nullptr;
    const std::string id = icuLocale(locale);
    const char* where = id.empty() ? nullptr : id.c_str();
    if (pattern) {
        const auto converted = utf16(icuPattern(pattern));
        format = udat_open(UDAT_PATTERN, UDAT_PATTERN, where, nullptr, 0, converted.c_str(), static_cast<int32_t>(converted.size()), &status);
    } else format = udat_open(time_style, date_style, where, nullptr, 0, nullptr, 0, &status);
    if (U_FAILURE(status) || !format) return {};
    UChar buffer[256];
    const int32_t length = udat_format(format, date, buffer, 256, nullptr, &status);
    udat_close(format);
    return U_FAILURE(status) ? std::wstring{} : wide(buffer, std::min<int32_t>(length, 256));
}
}

DWORD GetLastError() { return last_error; }
void SetLastError(DWORD error) { last_error = error; }

DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
DWORD GetCurrentThreadId() { return static_cast<DWORD>(syscall(SYS_gettid)); }
HANDLE GetCurrentThread() { return static_cast<HANDLE>(syscall(SYS_gettid)); }

BOOL SetThreadPriority(HANDLE thread, int priority) {
    // A thread's nice value: background work (lowest) yields to the UI and the games.
    int nice = 0;
    if (priority == THREAD_MODE_BACKGROUND_BEGIN || priority <= THREAD_PRIORITY_LOWEST) nice = 10;
    else if (priority == THREAD_PRIORITY_BELOW_NORMAL) nice = 5;
    else if (priority == THREAD_MODE_BACKGROUND_END || priority == THREAD_PRIORITY_NORMAL) nice = 0;
    else return TRUE; // Raising priority needs privileges the launcher does not ask for.
    return setpriority(PRIO_PROCESS, static_cast<id_t>(thread), nice) == 0 ? TRUE : FALSE;
}

EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE flags) {
    // The screen saver is inhibited while the launcher's window shows (SDL's default when the
    // display must stay on); ES_DISPLAY_REQUIRED keeps that, its absence allows it again.
    static EXECUTION_STATE previous = ES_CONTINUOUS;
    const EXECUTION_STATE old = previous;
    if (flags & ES_CONTINUOUS) previous = flags;
    if (flags & ES_DISPLAY_REQUIRED) SDL_DisableScreenSaver(); else if (flags & ES_CONTINUOUS) SDL_EnableScreenSaver();
    return old;
}

HMODULE GetModuleHandleW(const wchar_t*) { return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1)); }

DWORD GetModuleFileNameW(HMODULE, wchar_t* buffer, DWORD size) {
    const auto path = system_info::executablePath().wstring();
    if (!buffer || !size || path.empty()) return 0;
    const DWORD copied = std::min<DWORD>(size - 1, static_cast<DWORD>(path.size()));
    std::wmemcpy(buffer, path.c_str(), copied);
    buffer[copied] = L'\0';
    if (copied < path.size()) { last_error = ERROR_INSUFFICIENT_BUFFER; return size; }
    return copied;
}

DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size) {
    const auto value = name ? system_info::environment(name) : std::nullopt;
    if (!value) { last_error = 203; return 0; } // ERROR_ENVVAR_NOT_FOUND
    if (!buffer || value->size() + 1 > size) return static_cast<DWORD>(value->size() + 1);
    std::wmemcpy(buffer, value->c_str(), value->size() + 1);
    return static_cast<DWORD>(value->size());
}

BOOL SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value) {
    if (!name) return FALSE;
    const auto key = text_encoding::utf8Lenient(name);
    return (value ? setenv(key.c_str(), text_encoding::utf8Lenient(value).c_str(), 1) : unsetenv(key.c_str())) == 0;
}

DWORD ExpandEnvironmentStringsW(const wchar_t* source, wchar_t* destination, DWORD size) {
    std::wstring result;
    for (const wchar_t* p = source ? source : L""; *p;) {
        if (*p == L'%') {
            const wchar_t* end = std::wcschr(p + 1, L'%');
            if (end) {
                const std::wstring name(p + 1, end);
                if (const auto value = system_info::environment(name.c_str())) { result += *value; p = end + 1; continue; }
            }
        }
        result.push_back(*p++);
    }
    if (!destination || result.size() + 1 > size) return static_cast<DWORD>(result.size() + 1);
    std::wmemcpy(destination, result.c_str(), result.size() + 1);
    return static_cast<DWORD>(result.size() + 1);
}

BOOL GetUserNameW(wchar_t* buffer, DWORD* size) {
    const char* name = std::getenv("USER");
    const auto wide_name = text_encoding::wideLenient(name ? name : "");
    if (!size) return FALSE;
    if (!buffer || *size < wide_name.size() + 1) { *size = static_cast<DWORD>(wide_name.size() + 1); last_error = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    std::wmemcpy(buffer, wide_name.c_str(), wide_name.size() + 1);
    *size = static_cast<DWORD>(wide_name.size() + 1);
    return TRUE;
}

BOOL GetComputerNameW(wchar_t* buffer, DWORD* size) {
    char host[256]{};
    gethostname(host, sizeof(host) - 1);
    const auto wide_name = text_encoding::wideLenient(host);
    if (!size) return FALSE;
    if (!buffer || *size < wide_name.size() + 1) { *size = static_cast<DWORD>(wide_name.size() + 1); return FALSE; }
    std::wmemcpy(buffer, wide_name.c_str(), wide_name.size() + 1);
    *size = static_cast<DWORD>(wide_name.size());
    return TRUE;
}

void OutputDebugStringW(const wchar_t* text) { if (text) std::fputs(text_encoding::utf8Lenient(text).c_str(), stderr); }
void OutputDebugStringA(const char* text) { if (text) std::fputs(text, stderr); }

// ---- Time ----------------------------------------------------------------------------------

void GetLocalTime(SYSTEMTIME* time) {
    timespec now{}; clock_gettime(CLOCK_REALTIME, &now);
    std::tm parts{}; localtime_r(&now.tv_sec, &parts);
    *time = systemTime(parts, static_cast<int>(now.tv_nsec / 1000000));
}

void GetSystemTime(SYSTEMTIME* time) {
    timespec now{}; clock_gettime(CLOCK_REALTIME, &now);
    std::tm parts{}; gmtime_r(&now.tv_sec, &parts);
    *time = systemTime(parts, static_cast<int>(now.tv_nsec / 1000000));
}

void GetSystemTimeAsFileTime(FILETIME* time) {
    timespec now{}; clock_gettime(CLOCK_REALTIME, &now);
    *time = fileTime(static_cast<int64_t>(now.tv_sec) * 10000000 + now.tv_nsec / 100 + epoch_difference);
}

BOOL FileTimeToSystemTime(const FILETIME* file_time, SYSTEMTIME* system_time) {
    if (!file_time || !system_time) return FALSE;
    const int64_t value = fileTimeValue(*file_time) - epoch_difference;
    const std::time_t seconds = static_cast<std::time_t>(value / 10000000);
    std::tm parts{};
    if (!gmtime_r(&seconds, &parts)) return FALSE;
    *system_time = systemTime(parts, static_cast<int>((value % 10000000) / 10000));
    return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME* system_time, FILETIME* file_time) {
    if (!system_time || !file_time) return FALSE;
    std::tm parts{};
    parts.tm_year = system_time->wYear - 1900; parts.tm_mon = system_time->wMonth - 1; parts.tm_mday = system_time->wDay;
    parts.tm_hour = system_time->wHour; parts.tm_min = system_time->wMinute; parts.tm_sec = system_time->wSecond;
    const std::time_t seconds = timegm(&parts);
    *file_time = fileTime(static_cast<int64_t>(seconds) * 10000000 + system_time->wMilliseconds * 10000LL + epoch_difference);
    return TRUE;
}

BOOL FileTimeToLocalFileTime(const FILETIME* file_time, FILETIME* local_time) {
    if (!file_time || !local_time) return FALSE;
    const int64_t value = fileTimeValue(*file_time);
    const std::time_t seconds = static_cast<std::time_t>((value - epoch_difference) / 10000000);
    std::tm parts{};
    localtime_r(&seconds, &parts);
    *local_time = fileTime(value + static_cast<int64_t>(parts.tm_gmtoff) * 10000000);
    return TRUE;
}

BOOL SystemTimeToTzSpecificLocalTime(const void*, const SYSTEMTIME* universal, SYSTEMTIME* local) {
    FILETIME file{}, shifted{};
    return SystemTimeToFileTime(universal, &file) && FileTimeToLocalFileTime(&file, &shifted) && FileTimeToSystemTime(&shifted, local);
}

int GetDateFormatEx(const wchar_t* locale, DWORD flags, const SYSTEMTIME* date, const wchar_t* format, wchar_t* buffer, int size, const wchar_t*) {
    SYSTEMTIME now{};
    if (!date) { GetLocalTime(&now); date = &now; }
    const UDateFormatStyle style = (flags & DATE_LONGDATE) ? UDAT_LONG : UDAT_SHORT;
    std::wstring text;
    if (format) text = formatDate(*date, style, UDAT_NONE, format, locale);
    else if (flags & DATE_YEARMONTH) text = formatDate(*date, style, UDAT_NONE, L"LLLL yyyy", locale); // L: ICU's stand-alone month.
    else if (flags & DATE_MONTHDAY) text = formatDate(*date, style, UDAT_NONE, L"d MMMM", locale);
    else text = formatDate(*date, style, UDAT_NONE, nullptr, locale);
    return text.empty() ? 0 : copyOut(text, buffer, size);
}

int GetTimeFormatEx(const wchar_t* locale, DWORD flags, const SYSTEMTIME* time, const wchar_t* format, wchar_t* buffer, int size) {
    SYSTEMTIME now{};
    if (!time) { GetLocalTime(&now); time = &now; }
    std::wstring text;
    if (format) text = formatDate(*time, UDAT_NONE, UDAT_SHORT, format, locale);
    else if (flags & TIME_FORCE24HOURFORMAT) text = formatDate(*time, UDAT_NONE, UDAT_SHORT, (flags & TIME_NOSECONDS) ? L"HH:mm" : L"HH:mm:ss", locale);
    else text = formatDate(*time, UDAT_NONE, (flags & TIME_NOSECONDS) ? UDAT_SHORT : UDAT_MEDIUM, nullptr, locale);
    return text.empty() ? 0 : copyOut(text, buffer, size);
}

int GetUserDefaultLocaleName(wchar_t* buffer, int size) {
    const icu::Locale locale = icu::Locale::getDefault();
    std::string name = locale.getLanguage();
    if (*locale.getCountry()) name += std::string("-") + locale.getCountry();
    return copyOut(text_encoding::wideLenient(name.empty() ? "en-US" : name), buffer, size);
}

LANGID GetUserDefaultUILanguage() {
    const std::string language = icu::Locale::getDefault().getLanguage();
    if (language == "pl") return 0x0415;
    if (language == "pt") return 0x0416;
    if (language == "es") return 0x0C0A;
    if (language == "de") return 0x0407;
    if (language == "fr") return 0x040C;
    return 0x0409;
}

// ---- Text ----------------------------------------------------------------------------------

int LCMapStringEx(const wchar_t*, DWORD flags, const wchar_t* source, int source_length, wchar_t* destination, int destination_length,
                  void*, void*, LPARAM) {
    if (!source) return 0;
    std::wstring text(source, source_length < 0 ? std::wcslen(source) + 1 : static_cast<size_t>(source_length));
    if (flags & LCMAP_LOWERCASE) text = unicode_text::lower(text);
    else if (flags & LCMAP_UPPERCASE) for (auto& c : text) c = static_cast<wchar_t>(std::towupper(c));
    if (!destination || destination_length <= 0) return static_cast<int>(text.size());
    if (destination_length < static_cast<int>(text.size())) { last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::wmemcpy(destination, text.data(), text.size());
    return static_cast<int>(text.size());
}

int CompareStringEx(const wchar_t*, DWORD flags, const wchar_t* a, int a_length, const wchar_t* b, int b_length, void*, void*, LPARAM) {
    if (!a || !b) return 0;
    const std::wstring_view left(a, a_length < 0 ? std::wcslen(a) : static_cast<size_t>(a_length));
    const std::wstring_view right(b, b_length < 0 ? std::wcslen(b) : static_cast<size_t>(b_length));
    int result = 0;
    if (flags & (SORT_DIGITSASNUMBERS | LINGUISTIC_IGNORECASE | NORM_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC | NORM_IGNORENONSPACE))
        result = unicode_text::compareNatural(left, right);
    else result = left.compare(right);
    return result < 0 ? CSTR_LESS_THAN : result > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

int CompareStringOrdinal(const wchar_t* a, int a_length, const wchar_t* b, int b_length, BOOL ignore_case) {
    if (!a || !b) return 0;
    const std::wstring_view left(a, a_length < 0 ? std::wcslen(a) : static_cast<size_t>(a_length));
    const std::wstring_view right(b, b_length < 0 ? std::wcslen(b) : static_cast<size_t>(b_length));
    const int result = ignore_case ? unicode_text::compareIgnoreCase(left, right) : left.compare(right);
    return result < 0 ? CSTR_LESS_THAN : result > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL;
}

DWORD CharUpperBuffW(wchar_t* text, DWORD length) {
    if (!text) return 0;
    for (DWORD i = 0; i < length; ++i) text[i] = static_cast<wchar_t>(std::towupper(text[i]));
    return length;
}
DWORD CharLowerBuffW(wchar_t* text, DWORD length) {
    if (!text) return 0;
    for (DWORD i = 0; i < length; ++i) text[i] = static_cast<wchar_t>(std::towlower(text[i]));
    return length;
}
wchar_t* CharUpperW(wchar_t* text) { if (text) CharUpperBuffW(text, static_cast<DWORD>(std::wcslen(text))); return text; }
wchar_t* CharLowerW(wchar_t* text) { if (text) CharLowerBuffW(text, static_cast<DWORD>(std::wcslen(text))); return text; }
BOOL IsCharAlphaNumericW(wchar_t ch) { return unicode_text::isLetterOrDigit(ch); }
BOOL IsCharAlphaW(wchar_t ch) { return unicode_text::isLetter(ch); }

int MultiByteToWideChar(UINT, DWORD, const char* source, int source_length, wchar_t* destination, int destination_length) {
    if (!source) return 0;
    const auto wide_text = text_encoding::wideLenient(std::string_view(source, source_length < 0 ? std::strlen(source) + 1 : static_cast<size_t>(source_length)));
    if (!destination || destination_length <= 0) return static_cast<int>(wide_text.size());
    if (destination_length < static_cast<int>(wide_text.size())) { last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::wmemcpy(destination, wide_text.data(), wide_text.size());
    return static_cast<int>(wide_text.size());
}

int WideCharToMultiByte(UINT, DWORD, const wchar_t* source, int source_length, char* destination, int destination_length, const char*, BOOL* used_default) {
    if (used_default) *used_default = FALSE;
    if (!source) return 0;
    const auto narrow = text_encoding::utf8Lenient(std::wstring_view(source, source_length < 0 ? std::wcslen(source) + 1 : static_cast<size_t>(source_length)));
    if (!destination || destination_length <= 0) return static_cast<int>(narrow.size());
    if (destination_length < static_cast<int>(narrow.size())) { last_error = ERROR_INSUFFICIENT_BUFFER; return 0; }
    std::memcpy(destination, narrow.data(), narrow.size());
    return static_cast<int>(narrow.size());
}

// ---- Files ---------------------------------------------------------------------------------

DWORD GetFileAttributesW(const wchar_t* path) {
    std::error_code error;
    const auto status = std::filesystem::status(std::filesystem::path(path ? path : L""), error);
    if (error || !std::filesystem::exists(status)) { last_error = ERROR_FILE_NOT_FOUND; return INVALID_FILE_ATTRIBUTES; }
    DWORD attributes = std::filesystem::is_directory(status) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if ((status.permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none) attributes |= FILE_ATTRIBUTE_READONLY;
    const auto name = std::filesystem::path(path).filename().wstring();
    if (name.starts_with(L'.')) attributes |= FILE_ATTRIBUTE_HIDDEN;
    return attributes;
}

BOOL DeleteFileW(const wchar_t* path) {
    std::error_code error;
    if (!path || !std::filesystem::remove(std::filesystem::path(path), error)) { last_error = ERROR_FILE_NOT_FOUND; return FALSE; }
    return TRUE;
}

BOOL CopyFileW(const wchar_t* from, const wchar_t* to, BOOL fail_if_exists) {
    std::error_code error;
    if (!from || !to) return FALSE;
    const auto options = fail_if_exists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
    if (!std::filesystem::copy_file(std::filesystem::path(from), std::filesystem::path(to), options, error)) {
        last_error = error == std::errc::file_exists ? ERROR_FILE_EXISTS : ERROR_ACCESS_DENIED;
        return FALSE;
    }
    return TRUE;
}

BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags) {
    std::error_code error;
    if (!from || !to) return FALSE;
    const std::filesystem::path source(from), target(to);
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && std::filesystem::exists(target, error)) { last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    std::filesystem::rename(source, target, error);
    if (error && (flags & MOVEFILE_COPY_ALLOWED)) { // Across file systems: copy, then remove.
        error.clear();
        std::filesystem::copy(source, target, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, error);
        if (!error) std::filesystem::remove_all(source, error);
    }
    if (error) { last_error = ERROR_ACCESS_DENIED; return FALSE; }
    return TRUE;
}

BOOL CreateDirectoryW(const wchar_t* path, SECURITY_ATTRIBUTES*) {
    std::error_code error;
    if (!path) return FALSE;
    if (!std::filesystem::create_directory(std::filesystem::path(path), error)) { last_error = error ? ERROR_PATH_NOT_FOUND : ERROR_ALREADY_EXISTS; return FALSE; }
    return TRUE;
}

BOOL GetDiskFreeSpaceExW(const wchar_t* directory, ULARGE_INTEGER* free_to_caller, ULARGE_INTEGER* total, ULARGE_INTEGER* total_free) {
    struct statvfs info{};
    if (!directory || statvfs(text_encoding::utf8Lenient(directory).c_str(), &info) != 0) { last_error = ERROR_PATH_NOT_FOUND; return FALSE; }
    if (free_to_caller) free_to_caller->QuadPart = static_cast<ULONGLONG>(info.f_bavail) * info.f_frsize;
    if (total) total->QuadPart = static_cast<ULONGLONG>(info.f_blocks) * info.f_frsize;
    if (total_free) total_free->QuadPart = static_cast<ULONGLONG>(info.f_bfree) * info.f_frsize;
    return TRUE;
}

BOOL GetSystemPowerStatus(SYSTEM_POWER_STATUS* status) {
    if (!status) return FALSE;
    int seconds = -1, percent = -1;
    const SDL_PowerState power = SDL_GetPowerInfo(&seconds, &percent);
    status->ACLineStatus = power == SDL_POWERSTATE_ON_BATTERY ? 0 : (power == SDL_POWERSTATE_UNKNOWN ? 255 : 1);
    status->BatteryFlag = power == SDL_POWERSTATE_NO_BATTERY ? 128 : power == SDL_POWERSTATE_CHARGING ? 8 : (percent >= 0 && percent < 20 ? 2 : 1);
    status->BatteryLifePercent = percent >= 0 ? static_cast<BYTE>(percent) : 255;
    status->SystemStatusFlag = 0;
    status->BatteryLifeTime = seconds >= 0 ? static_cast<DWORD>(seconds) : static_cast<DWORD>(-1);
    status->BatteryFullLifeTime = static_cast<DWORD>(-1);
    return TRUE;
}

// ---- Movable memory ------------------------------------------------------------------------

namespace {
struct Block { size_t size; int locks; alignas(16) unsigned char data[1]; };
Block* block(HGLOBAL memory) { return reinterpret_cast<Block*>(memory); }
}

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes) {
    auto* created = static_cast<Block*>(std::malloc(offsetof(Block, data) + std::max<size_t>(bytes, 1)));
    if (!created) { last_error = ERROR_NOT_ENOUGH_MEMORY; return nullptr; }
    created->size = bytes;
    created->locks = 0;
    if (flags & GMEM_ZEROINIT) std::memset(created->data, 0, bytes);
    return reinterpret_cast<HGLOBAL>(created);
}
LPVOID GlobalLock(HGLOBAL memory) { if (!memory) return nullptr; ++block(memory)->locks; return block(memory)->data; }
BOOL GlobalUnlock(HGLOBAL memory) { if (!memory) return FALSE; if (block(memory)->locks > 0) --block(memory)->locks; return block(memory)->locks > 0; }
SIZE_T GlobalSize(HGLOBAL memory) { return memory ? block(memory)->size : 0; }
HGLOBAL GlobalFree(HGLOBAL memory) { std::free(memory); return nullptr; }
HLOCAL LocalFree(HLOCAL memory) { std::free(memory); return nullptr; }
