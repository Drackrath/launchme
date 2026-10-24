#pragma once
#include <Windows.h>
#include <winhttp.h>

namespace winhttp {
// One process-wide session lets WinHTTP pool keep-alive TLS connections per
// host instead of paying DNS + TCP + TLS for every cover or API request.
// Deliberately never closed: detached workers may still be finishing a request
// during process exit, and the OS reclaims the handle anyway.
inline HINTERNET session() {
    static const HINTERNET shared = WinHttpOpen(L"launchme/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    return shared;
}

// A shared session would otherwise share one cookie jar between unrelated
// services; requests stay as isolated as they were with per-request sessions.
inline void isolate(HINTERNET request) {
    DWORD feature = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));
}

struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET handle = nullptr) : value(handle) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value != nullptr; }
};
}
