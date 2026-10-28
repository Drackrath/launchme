#include "aurelia.hpp"
#include "aurelia_stream.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <algorithm>
#include <map>
#include <fstream>

namespace aurelia {
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
bool longCommand(Command c) {
    return c == Command::install || c == Command::update || c == Command::verify || c == Command::downgrade ||
           c == Command::move || c == Command::workshop_install || c == Command::workshop_subscribe || c == Command::play || c == Command::chat_live;
}
bool daemonIdentified(const std::wstring& endpoint) {
    // Upstream's stale-daemon fallback can stop every Aurelia daemon when its marker is
    // missing. Never enter that path for an existing endpoint without a usable PID marker.
    if (!WaitNamedPipeW(endpoint.c_str(), 1)) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;
        if (error != ERROR_SEM_TIMEOUT && error != ERROR_PIPE_BUSY) return false;
    }
    auto name = endpoint;
    for (auto& c : name) if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.')) c = L'-';
    std::error_code error; const auto temp = std::filesystem::temp_directory_path(error); if (error) return false;
    std::ifstream file(temp / (name + L".info"), std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 65536) return false;
    std::string content(static_cast<size_t>(file.tellg()), '\0'); file.seekg(0);
    if (!file.read(content.data(), content.size())) return false;
    const auto value = json::parse(content); const auto pid = value ? integer(value->get("pid")) : std::nullopt;
    return pid && *pid > 0 && *pid <= UINT32_MAX && !text(*value, "version").empty();
}
}
std::wstring daemonEndpoint(const std::filesystem::path& profile) {
    if (!profile.is_absolute()) return {};
    std::error_code error; auto canonical = std::filesystem::weakly_canonical(profile, error);
    if (error) return {};
    auto path = canonical.make_preferred().native(); std::transform(path.begin(), path.end(), path.begin(), towlower);
    Handle token; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return {};
    DWORD count = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &count);
    std::vector<uint8_t> bytes(count);
    if (!count || !GetTokenInformation(token.value, TokenUser, bytes.data(), count, &count)) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return {};
    path += L'\n'; path += sid; LocalFree(sid);
    const auto identity = text_encoding::utf8(path);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    uint8_t hash[32]{};
    const auto result = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(identity.data())),
                                  static_cast<ULONG>(identity.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result < 0 ? std::wstring{} : L"\\\\.\\pipe\\launchme-aurelia-" + text_encoding::wideLenient(key_codec::encode({reinterpret_cast<char*>(hash), sizeof(hash)}));
}
Reply invoke(const Runtime& runtime, const Request& request, const Cancel& cancel, const Event& event) {
    Reply result;
    const auto args = arguments(request);
    if (args.empty() || !runtime.executable.is_absolute() || !runtime.profile.is_absolute()) {
        result.error = tr(L"Nieprawidłowe polecenie Aurelii"); return result;
    }
    if (cancel && cancel()) { result.cancelled = true; return result; }
    std::error_code ec; std::filesystem::create_directories(runtime.profile, ec);
    if (ec) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    // A private environment block for this child. Nothing changes the parent's environment,
    // and an existing standalone Aurelia session is never imported.
    std::map<std::wstring, std::wstring> env;
    auto original = GetEnvironmentStringsW(); if (!original) return result;
    for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
        const std::wstring entry(p); const auto at = entry.find(L'=', 1); if (at == entry.npos) continue;
        auto key = entry.substr(0, at); std::transform(key.begin(), key.end(), key.begin(), towupper);
        if (!key.starts_with(L"AURELIA_")) env[key] = entry.substr(at + 1);
    }
    FreeEnvironmentStringsW(original);
    env[L"AURELIA_CONFIG_DIR"] = runtime.profile.native();
    const auto endpoint = daemonEndpoint(runtime.profile);
    if (endpoint.empty()) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    env[L"AURELIA_DAEMON_SOCKET"] = endpoint;
    // `stop` and `running` only read Aurelia's launch records and end the recorded process: run in
    // this child, not forwarded to the daemon, which (v0.1.40) answers nothing while it waits for a game.
    const bool local = request.command == Command::stop || request.command == Command::running;
    if (local) env[L"AURELIA_NO_DAEMON"] = L"1";
    else if (request.command != Command::login && !daemonIdentified(endpoint)) {
        result.error = tr(L"Nie można potwierdzić tożsamości daemona Aurelii. Spróbuj ponownie po zakończeniu jego uruchamiania."); return result;
    }
    env[L"NO_COLOR"] = L"1";
    std::vector<wchar_t> environment;
    for (const auto& [key, value] : env) {
        const auto entry = key + L'=' + value; environment.insert(environment.end(), entry.begin(), entry.end()); environment.push_back(0);
    }
    environment.push_back(0);
    std::wstring command = command_line::quoteArgument(runtime.executable.native());
    for (const auto& arg : args) command += L' ' + command_line::quoteArgument(arg);
    if (command.size() >= 32767) { result.error = tr(L"Polecenie jest zbyt długie"); return result; }

    const bool console = request.command == Command::login, live = request.command == Command::chat_live;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle out_read, out_write, err_read, err_write, in_read, in_write;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = console ? sizeof(STARTUPINFOW) : sizeof(startup);
    std::vector<uint8_t> attributes;
    if (!console) {
        if (!CreatePipe(&out_read.value, &out_write.value, &sa, 65536) || !CreatePipe(&err_read.value, &err_write.value, &sa, 65536) ||
            !CreatePipe(&in_read.value, &in_write.value, &sa, 0)) return result;
        SetHandleInformation(out_read.value, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(err_read.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(in_write.value, HANDLE_FLAG_INHERIT, 0);
        if (!live) in_write.reset(); // Queries cannot wait for a prompt. Live chat keeps stdin open.
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = in_read.value; startup.StartupInfo.hStdOutput = out_write.value; startup.StartupInfo.hStdError = err_write.value;
        SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size); attributes.resize(size);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) return result;
        HANDLE handles[]{in_read.value, out_write.value, err_write.value};
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(startup.lpAttributeList); return result;
        }
    }
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | (console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT);
    const bool created = CreateProcessW(runtime.executable.c_str(), command.data(), nullptr, nullptr, !console, flags,
                                       environment.data(), runtime.profile.c_str(), &startup.StartupInfo, &pi) != FALSE;
    if (!console) DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) { result.error = tr(L"Nie można uruchomić Aurelii"); return result; }
    Handle process{pi.hProcess}, thread{pi.hThread};
    struct ChildScope {
        HANDLE process; bool exited = false;
        ~ChildScope() { if (!exited && WaitForSingleObject(process, 0) != WAIT_OBJECT_0) { TerminateProcess(process, 1); WaitForSingleObject(process, 3000); } }
    } child{process.value}; // Also cleans up if an output consumer throws.
    out_write.reset(); err_write.reset(); in_read.reset();
    // Do not place the thin client in a kill-on-close job: its daemon and launched games
    // are independent. Cancellation closes only the owned CLI process, never the game/Steam.
    JsonStream stdout_json, stderr_json;
    std::optional<json::Value> stdout_result, stderr_result;
    size_t output_bytes = 0;
    bool overflow = false;
    constexpr size_t limit = 16 * 1024 * 1024;
    const auto drain = [&](HANDLE pipe, bool stdout_pipe) {
        if (!pipe) return;
        for (int reads = 0; reads < 64; ++reads) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[16384];
            if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            if (stdout_pipe && !live && (output_bytes += received) > limit) { overflow = true; return; }
            auto& parser = stdout_pipe ? stdout_json : stderr_json;
            if (!parser.feed({buffer, received}, [&](const json::Value& item) {
                if (event) event(item);
                if (item.get("error")) result.error = text_encoding::wideLenient(text(item, "error"));
                const auto kind = text(item, "event");
                if (kind.empty() || kind == "result" || item.get("error")) (stdout_pipe ? stdout_result : stderr_result) = item;
            })) { overflow = true; return; }
        }
    };
    const auto started = GetTickCount64();
    const uint64_t timeout = console ? 10 * 60 * 1000ULL : longCommand(request.command) ? 0 : runtime.query_timeout_ms;
    while (true) {
        drain(out_read.value, true); drain(err_read.value, false);
        if ((cancel && cancel()) || overflow || (timeout && GetTickCount64() - started > timeout)) {
            result.cancelled = cancel && cancel();
            result.error = result.cancelled ? tr(L"Anulowano") : overflow ? tr(L"Odpowiedź Aurelii jest zbyt duża") : tr(L"Aurelia nie odpowiedziała na czas");
            in_write.reset();
            if (WaitForSingleObject(process.value, live ? 800 : 0) != WAIT_OBJECT_0) TerminateProcess(process.value, 1);
            WaitForSingleObject(process.value, 3000); return result;
        }
        if (WaitForSingleObject(process.value, 40) == WAIT_OBJECT_0) break;
    }
    drain(out_read.value, true); drain(err_read.value, false);
    child.exited = true;
    DWORD code = 1; GetExitCodeProcess(process.value, &code);
    if (console) { result.ok = code == 0; if (!result.ok) result.error = tr(L"Logowanie Aurelii nie powiodło się"); return result; }
    auto parsed = stdout_result ? std::move(stdout_result) : std::move(stderr_result);
    if (parsed) result.data = std::move(*parsed);
    if (result.data.get("error")) result.error = text_encoding::wideLenient(text(result.data, "error"));
    result.ok = code == 0 && !overflow && !stdout_json.incomplete() && (parsed.has_value() || live) && result.error.empty();
    if (!result.ok && result.error.empty()) result.error = tr(L"Polecenie Aurelii nie powiodło się");
    return result;
}
Transport transport(Runtime runtime) {
    return [runtime = std::move(runtime)](const Request& r, const Cancel& c, const Event& e) { return invoke(runtime, r, c, e); };
}
}
