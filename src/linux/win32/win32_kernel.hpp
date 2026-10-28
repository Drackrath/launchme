#pragma once
// Linux only: the kernel32/advapi32 calls the launcher's UI code makes, as POSIX/ICU ports
// (src/linux/window/kernel.cpp). Shared code below src/app and src/ui uses platform/ instead.
#include "win32_base.hpp"

// HANDLE is a process or thread ID on Linux, as platform/launch.hpp declares it; kernel objects
// (mutexes, events, process handles) have no stand-in: code that needs them is ported instead.
using HANDLE = pid_t;

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_DRIVE 15L
#define ERROR_BAD_NETPATH 53L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_CANCELLED 1223L
#define ERROR_CLASS_ALREADY_EXISTS 1410L
#define ERROR_NO_ASSOCIATION 1155L
#define ERROR_DDE_FAIL 1156L
#ifndef E_NOT_SUFFICIENT_BUFFER
#define E_NOT_SUFFICIENT_BUFFER ((HRESULT)0x8007007A)
#endif

#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED 0xFFFFFFFFu

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#ifndef GENERIC_READ
#define GENERIC_READ (0x80000000L)
#define GENERIC_WRITE (0x40000000L)
#endif
#define MAXDWORD 0xffffffffu

#define SYNCHRONIZE 0x00100000L
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_TERMINATE 0x0001
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_MODE_BACKGROUND_BEGIN 0x00010000
#define THREAD_MODE_BACKGROUND_END 0x00020000

#define CP_ACP 0
#define CP_UTF8 65001
#define LOCALE_NAME_USER_DEFAULT nullptr
#define LOCALE_NAME_INVARIANT L""
#define LOCALE_NAME_MAX_LENGTH 85
#define LCMAP_LOWERCASE 0x00000100
#define LCMAP_UPPERCASE 0x00000200
#define LCMAP_LINGUISTIC_CASING 0x01000000
#define NORM_IGNORECASE 0x00000001
#define NORM_IGNORENONSPACE 0x00000002
#define NORM_IGNORESYMBOLS 0x00000004
#define LINGUISTIC_IGNORECASE 0x00000010
#define LINGUISTIC_IGNOREDIACRITIC 0x00000020
#define SORT_DIGITSASNUMBERS 0x00000008
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
#define DATE_SHORTDATE 0x00000001
#define DATE_LONGDATE 0x00000002
#define DATE_YEARMONTH 0x00000008
#define DATE_MONTHDAY 0x00000080
#define TIME_NOSECONDS 0x00000002
#define TIME_NOMINUTESORSECONDS 0x00000001
#define TIME_FORCE24HOURFORMAT 0x00000008

#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)

#define ES_CONTINUOUS 0x80000000u
#define ES_SYSTEM_REQUIRED 0x00000001u
#define ES_DISPLAY_REQUIRED 0x00000002u
#define ES_AWAYMODE_REQUIRED 0x00000040u
using EXECUTION_STATE = DWORD;

struct SECURITY_ATTRIBUTES { DWORD nLength; void* lpSecurityDescriptor; BOOL bInheritHandle; };
struct SYSTEM_POWER_STATUS {
    BYTE ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag;
    DWORD BatteryLifeTime, BatteryFullLifeTime;
};

DWORD GetLastError();
void SetLastError(DWORD error);
DWORD GetCurrentProcessId();
DWORD GetCurrentThreadId();
// The calling thread's ID; SetThreadPriority maps the priority to the thread's nice value.
HANDLE GetCurrentThread();
BOOL SetThreadPriority(HANDLE thread, int priority);
EXECUTION_STATE SetThreadExecutionState(EXECUTION_STATE flags);

HMODULE GetModuleHandleW(const wchar_t* name);
DWORD GetModuleFileNameW(HMODULE module, wchar_t* buffer, DWORD size);
DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size);
BOOL SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value);
DWORD ExpandEnvironmentStringsW(const wchar_t* source, wchar_t* destination, DWORD size);
BOOL GetUserNameW(wchar_t* buffer, DWORD* size);
BOOL GetComputerNameW(wchar_t* buffer, DWORD* size);
void OutputDebugStringW(const wchar_t* text);
void OutputDebugStringA(const char* text);

void GetLocalTime(SYSTEMTIME* time);
void GetSystemTime(SYSTEMTIME* time);
void GetSystemTimeAsFileTime(FILETIME* time);
BOOL FileTimeToSystemTime(const FILETIME* file_time, SYSTEMTIME* system_time);
BOOL SystemTimeToFileTime(const SYSTEMTIME* system_time, FILETIME* file_time);
BOOL FileTimeToLocalFileTime(const FILETIME* file_time, FILETIME* local_time);
BOOL SystemTimeToTzSpecificLocalTime(const void* zone, const SYSTEMTIME* universal, SYSTEMTIME* local);
int GetDateFormatEx(const wchar_t* locale, DWORD flags, const SYSTEMTIME* date, const wchar_t* format, wchar_t* buffer, int size, const wchar_t* calendar);
int GetTimeFormatEx(const wchar_t* locale, DWORD flags, const SYSTEMTIME* time, const wchar_t* format, wchar_t* buffer, int size);
int GetUserDefaultLocaleName(wchar_t* buffer, int size);
LANGID GetUserDefaultUILanguage();

int LCMapStringEx(const wchar_t* locale, DWORD flags, const wchar_t* source, int source_length, wchar_t* destination, int destination_length,
                  void* version, void* reserved, LPARAM sort_handle);
int CompareStringEx(const wchar_t* locale, DWORD flags, const wchar_t* a, int a_length, const wchar_t* b, int b_length,
                    void* version, void* reserved, LPARAM sort_handle);
int CompareStringOrdinal(const wchar_t* a, int a_length, const wchar_t* b, int b_length, BOOL ignore_case);
DWORD CharUpperBuffW(wchar_t* text, DWORD length);
DWORD CharLowerBuffW(wchar_t* text, DWORD length);
wchar_t* CharUpperW(wchar_t* text);
wchar_t* CharLowerW(wchar_t* text);
BOOL IsCharAlphaNumericW(wchar_t ch);
BOOL IsCharAlphaW(wchar_t ch);
int MultiByteToWideChar(UINT code_page, DWORD flags, const char* source, int source_length, wchar_t* destination, int destination_length);
int WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t* source, int source_length, char* destination, int destination_length,
                        const char* default_char, BOOL* used_default);

DWORD GetFileAttributesW(const wchar_t* path);
BOOL DeleteFileW(const wchar_t* path);
BOOL CopyFileW(const wchar_t* from, const wchar_t* to, BOOL fail_if_exists);
BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags);
BOOL CreateDirectoryW(const wchar_t* path, SECURITY_ATTRIBUTES* attributes);
BOOL GetDiskFreeSpaceExW(const wchar_t* directory, ULARGE_INTEGER* free_to_caller, ULARGE_INTEGER* total, ULARGE_INTEGER* total_free);
BOOL GetSystemPowerStatus(SYSTEM_POWER_STATUS* status);

// MSVC CRT: case-insensitive comparisons. Both fold with towlower in the C locale, as MSVC's
// do by default (ASCII letters only).
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t count) { return wcsncasecmp(a, b, count); }

// WinNls.h: UTF-16 surrogates. A UTF-32 wchar_t is never one, but code testing for them stays as
// it is on Windows.
#define HIGH_SURROGATE_START 0xd800
#define HIGH_SURROGATE_END 0xdbff
#define LOW_SURROGATE_START 0xdc00
#define LOW_SURROGATE_END 0xdfff
#define IS_HIGH_SURROGATE(wch) (((wch) >= HIGH_SURROGATE_START) && ((wch) <= HIGH_SURROGATE_END))
#define IS_LOW_SURROGATE(wch) (((wch) >= LOW_SURROGATE_START) && ((wch) <= LOW_SURROGATE_END))
#define IS_SURROGATE_PAIR(hs, ls) (IS_HIGH_SURROGATE(hs) && IS_LOW_SURROGATE(ls))

// Movable memory blocks (the clipboard's CF_UNICODETEXT, stream contents).
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);
SIZE_T GlobalSize(HGLOBAL memory);
HGLOBAL GlobalFree(HGLOBAL memory);
HLOCAL LocalFree(HLOCAL memory);
