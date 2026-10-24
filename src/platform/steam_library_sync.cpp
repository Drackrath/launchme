#include "steam_library_sync.hpp"
#include "steam_library_bridge.hpp"
#include "text_encoding.hpp"
#include "../net/json.hpp"
#include <WinSock2.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>

namespace steam_sync {
namespace {
constexpr size_t max_reply = 4 * 1024 * 1024;
struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET h = nullptr) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
};
std::string quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
const json::Value* field(const json::Value* value, std::string_view key) { return value ? value->get(key) : nullptr; }
std::string text(const json::Value* value) { return value ? value->stringOr() : std::string{}; }
bool flag(const json::Value* value) { return value && value->type == json::Value::Type::boolean && value->flag; }
bool ids(const json::Value* value, std::vector<uint32_t>& output) {
    if (!value || value->type != json::Value::Type::array || value->elements.size() > 200000) return false;
    for (const auto& entry : value->elements) {
        if (entry.type != json::Value::Type::number || entry.numeric < 1 || entry.numeric > UINT32_MAX || std::floor(entry.numeric) != entry.numeric) return false;
        output.push_back(static_cast<uint32_t>(entry.numeric));
    }
    return true;
}
// A page title/URL alone is not process identity. Admit only a loopback listener of the installed Steam web helper.
bool steamEndpoint(const std::filesystem::path& root) {
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (!size || size > 4 * 1024 * 1024) return false;
    std::vector<uint8_t> bytes(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return false;
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (row.dwLocalAddr != htonl(INADDR_LOOPBACK) || ntohs(static_cast<u_short>(row.dwLocalPort)) != 8080) continue;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.dwOwningPid);
        if (!process) return false;
        wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
        const bool read = QueryFullProcessImageNameW(process, 0, path, &length); CloseHandle(process);
        auto expected = (root / L"bin" / L"cef" / L"cef.win64" / L"steamwebhelper.exe").lexically_normal();
        expected.make_preferred();
        return read && _wcsicmp(path, expected.c_str()) == 0;
    }
    return false;
}
std::optional<std::string> targets(HINTERNET connection) {
    Handle request(WinHttpOpenRequest(connection, L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request.value || !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) || status != 200) return {};
    std::string result; char bytes[8192]; DWORD read = 0;
    do {
        if (!WinHttpReadData(request.value, bytes, sizeof(bytes), &read) || result.size() + read > max_reply) return {};
        result.append(bytes, read);
    } while (read);
    return result;
}
}
std::string requestExpression(uint64_t account, const std::vector<Change>& changes) {
    std::string request = "{\"account\":" + quote(std::to_string(account)) + ",\"changes\":[";
    for (size_t i = 0; i < changes.size(); ++i) {
        if (i) request += ',';
        const auto& c = changes[i];
        request += "{\"kind\":" + quote(c.kind == Kind::name ? "name" : c.kind == Kind::hidden ? "hidden" : c.kind == Kind::create ? "create" : "member") +
            ",\"collection\":" + quote(c.collection) + ",\"app\":" + std::to_string(c.app) + ",\"value\":" + (c.value ? "true" : "false") +
            ",\"name\":" + quote(text_encoding::utf8(c.name)) + '}';
    }
    return "(" + std::string(steam_library_bridge) + ")(" + request + "]}).then(JSON.stringify)";
}
Result parseResult(uint64_t account, const std::vector<Change>& changes, std::string_view source) {
    Result out; out.account = account;
    const auto value = json::parse(source, max_reply);
    if (!value) { out.error = "invalid-reply"; return out; }
    if (!flag(value->get("ok"))) { out.error = text(value->get("error")); if (out.error.empty()) out.error = "invalid-reply"; return out; }
    if (text(value->get("account")) != std::to_string(account)) { out.error = "account-changed"; return out; }
    Snapshot snapshot; snapshot.account = account;
    const auto* collections = value->get("collections");
    if (!collections || collections->type != json::Value::Type::array || collections->elements.size() > 10000) { out.error = "invalid-reply"; return out; }
    std::set<std::string> seen;
    for (const auto& item : collections->elements) {
        SteamCollection c; c.id = text(item.get("id")); c.name = text_encoding::wide(text(item.get("name")));
        if (!c.id.starts_with("user-collections.") || c.id == "user-collections.hidden" || c.id.size() > 1024 || c.name.empty() ||
            !seen.insert(c.id).second || !ids(item.get("members"), c.app_ids)) { out.error = "invalid-reply"; return out; }
        if (c.id == "user-collections.favorite") c.name = tr(L"Ulubione");
        std::sort(c.app_ids.begin(), c.app_ids.end());
        c.app_ids.erase(std::unique(c.app_ids.begin(), c.app_ids.end()), c.app_ids.end());
        snapshot.collections.push_back(std::move(c));
    }
    std::vector<uint32_t> hidden, private_apps;
    if (!ids(value->get("hidden"), hidden) || !ids(value->get("privateApps"), private_apps)) { out.error = "invalid-reply"; return out; }
    snapshot.hidden.insert(hidden.begin(), hidden.end()); snapshot.private_apps.insert(private_apps.begin(), private_apps.end());
    const auto* ack = value->get("ack");
    if (!ack || ack->type != json::Value::Type::array) { out.error = "invalid-reply"; return out; }
    std::set<size_t> applied;
    for (const auto& index : ack->elements) {
        if (index.type != json::Value::Type::number || index.numeric < 0 || index.numeric >= changes.size() || std::floor(index.numeric) != index.numeric) {
            out.error = "invalid-reply"; out.applied.clear(); return out;
        }
        const auto at = static_cast<size_t>(index.numeric);
        if (!applied.insert(at).second) { out.error = "invalid-reply"; out.applied.clear(); return out; }
        out.applied.push_back(changes[at]);
    }
    out.snapshot = std::move(snapshot); out.error = text(value->get("error")); out.cloud_pending = flag(value->get("cloudPending"));
    return out;
}
Result exchange(const std::filesystem::path& root, uint64_t account, const std::vector<Change>& changes) {
    Result failed; failed.account = account; failed.error = "unavailable";
    if (!root.is_absolute() || account <= 76561197960265728ULL || changes.size() > 16 || !steamEndpoint(root)) return failed;
    Handle session(WinHttpOpen(L"Launchme Steam library sync", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
    if (!session.value) return failed;
    WinHttpSetTimeouts(session.value, 1500, 1500, 3000, 10000);
    Handle connection(WinHttpConnect(session.value, L"127.0.0.1", 8080, 0));
    const auto listing = connection.value ? targets(connection.value) : std::nullopt;
    const auto parsed = listing ? json::parse(*listing, max_reply) : std::nullopt;
    if (!parsed || parsed->type != json::Value::Type::array) return failed;
    std::string path;
    for (const auto& page : parsed->elements) {
        if (text(page.get("title")) != "SharedJSContext" || !text(page.get("url")).starts_with("https://steamloopback.host/")) continue;
        const auto id = text(page.get("id"));
        if (id.empty() || id.size() > 128 || id.find_first_not_of("0123456789abcdefABCDEF-") != id.npos || !path.empty()) return failed;
        path = "/devtools/page/" + id;
    }
    if (path.empty()) return failed;
    Handle request(WinHttpOpenRequest(connection.value, L"GET", text_encoding::wide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return failed;
    Handle socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
    if (!socket.value) return failed;
    DWORD timeout = 10000; WinHttpSetOption(socket.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    const auto payload = "{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":" + quote(requestExpression(account, changes)) +
        ",\"returnByValue\":true,\"awaitPromise\":true,\"timeout\":9000}}";
    if (payload.size() > max_reply || WinHttpWebSocketSend(socket.value, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size())) != NO_ERROR) return failed;
    std::string message;
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        char buffer[16384]; DWORD size = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
        if (WinHttpWebSocketReceive(socket.value, buffer, sizeof(buffer), &size, &kind) != NO_ERROR || kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE || message.size() + size > max_reply) return failed;
        message.append(buffer, size);
        if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
        const auto reply = json::parse(message, max_reply);
        if (!reply) return failed;
        if (reply->get("id") && reply->get("id")->intOr() == 1) {
            const auto* result = reply->get("result");
            if (reply->get("error") || field(result, "exceptionDetails")) { failed.error = "unsupported"; return failed; }
            // stringify inside CDP keeps the existing parser simple and preserves integer IDs exactly.
            const auto* body = field(field(result, "result"), "value");
            if (!body) return failed;
            // The bridge result is embedded JSON. Locate it by asking the expression to return JSON text instead.
            if (body->type != json::Value::Type::string) { failed.error = "invalid-reply"; return failed; }
            WinHttpWebSocketClose(socket.value, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            return parseResult(account, changes, body->text);
        }
        message.clear();
    }
    return failed;
}
}
