#include "remote_server.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <random>

namespace remote {
namespace {
constexpr size_t head_limit = 16 * 1024, body_limit = 64 * 1024;
constexpr int connection_limit = 12;
constexpr auto read_budget = std::chrono::seconds(8);

const char* reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}
// Readable within 100 ms; false: not yet (the caller checks its budget and `stopping`).
bool readable(SOCKET socket) {
    fd_set set; FD_ZERO(&set); FD_SET(socket, &set);
    timeval wait{0, 100 * 1000};
    return select(0, &set, nullptr, nullptr, &wait) == 1;
}
bool sendAll(SOCKET socket, std::string_view data) {
    while (!data.empty()) {
        const int sent = send(socket, data.data(), static_cast<int>(std::min<size_t>(data.size(), 64 * 1024)), 0);
        if (sent <= 0) return false;
        data.remove_prefix(static_cast<size_t>(sent));
    }
    return true;
}
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return out;
}
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
} // namespace

std::string Request::param(std::string_view name) const {
    const auto found = query.find(name);
    return found == query.end() ? std::string{} : found->second;
}

std::string decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    const auto hex = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') out += ' ';
        else if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
            out += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
            i += 2;
        } else out += text[i];
    }
    return out;
}

std::optional<Request> parseHead(std::string_view head, size_t& length) {
    length = 0;
    const auto line_end = head.find("\r\n");
    const auto line = head.substr(0, line_end);
    const auto first = line.find(' '), second = first == std::string_view::npos ? first : line.find(' ', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos || !line.substr(second + 1).starts_with("HTTP/1.")) return std::nullopt;
    Request request;
    request.method = std::string(line.substr(0, first));
    auto target = line.substr(first + 1, second - first - 1);
    if (!target.starts_with('/')) return std::nullopt;
    const auto question = target.find('?');
    request.path = decode(target.substr(0, question));
    if (question != std::string_view::npos) {
        auto rest = target.substr(question + 1);
        while (!rest.empty()) {
            const auto amp = rest.find('&');
            const auto pair = rest.substr(0, amp);
            const auto equals = pair.find('=');
            if (!pair.empty()) request.query[decode(pair.substr(0, equals))] = equals == std::string_view::npos ? std::string{} : decode(pair.substr(equals + 1));
            if (amp == std::string_view::npos) break;
            rest.remove_prefix(amp + 1);
        }
    }
    for (size_t at = line_end; at != std::string_view::npos && at < head.size();) {
        const auto start = at + 2;
        const auto end = head.find("\r\n", start);
        const auto header = head.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (const auto colon = header.find(':'); colon != std::string_view::npos && lower(trim(header.substr(0, colon))) == "content-length") {
            const auto value = trim(header.substr(colon + 1));
            size_t parsed = 0;
            for (const char ch : value) {
                if (ch < '0' || ch > '9' || parsed > body_limit) return std::nullopt;
                parsed = parsed * 10 + static_cast<size_t>(ch - '0');
            }
            length = parsed;
        }
        at = end;
    }
    return request;
}

Server::~Server() { stop(); }

bool Server::start(uint16_t port, Handler handler, bool loopback_only) {
    stop();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsock_ = true;
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) { stop(); return false; }
    BOOL exclusive = TRUE; // Nobody else may take the same port beside it.
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 16) != 0) {
        closesocket(listener);
        stop();
        return false;
    }
    listener_ = static_cast<uintptr_t>(listener);
    port_ = port;
    handler_ = std::move(handler);
    stopping_ = false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Server::stop() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
    {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [&] { return active_ == 0; });
    }
    if (listener_ != ~uintptr_t{0}) { closesocket(static_cast<SOCKET>(listener_)); listener_ = ~uintptr_t{0}; }
    if (winsock_) { WSACleanup(); winsock_ = false; }
    handler_ = {};
    port_ = 0;
}

void Server::run() {
    const auto listener = static_cast<SOCKET>(listener_);
    while (!stopping_) {
        if (!readable(listener)) continue;
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        {
            std::lock_guard lock(mutex_);
            if (active_ >= connection_limit) { closesocket(client); continue; }
            ++active_;
        }
        std::thread([this, client] {
            serve(static_cast<uintptr_t>(client));
            closesocket(client);
            std::lock_guard lock(mutex_);
            --active_;
            idle_.notify_all();
        }).detach(); // stop() waits for every one of them.
    }
}

void Server::serve(uintptr_t handle) {
    const auto client = static_cast<SOCKET>(handle);
    const DWORD send_timeout = 10000;
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout), sizeof(send_timeout));
    const auto deadline = std::chrono::steady_clock::now() + read_budget;
    std::string data;
    size_t head_end = std::string::npos;
    std::optional<Request> request;
    size_t length = 0;
    const auto respond = [&](const Response& response) {
        std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " + reason(response.status) + "\r\n";
        out += "Content-Type: " + response.type + "\r\nContent-Length: " + std::to_string(response.body.size()) + "\r\n";
        out += "Cache-Control: " + response.cache + "\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n";
        if (!request || request->method != "HEAD") out += response.body;
        sendAll(client, out);
        shutdown(client, SD_SEND);
    };
    char buffer[8192];
    while (true) {
        if (stopping_ || std::chrono::steady_clock::now() > deadline) return;
        if (head_end == std::string::npos) {
            head_end = data.find("\r\n\r\n");
            if (head_end != std::string::npos) {
                request = parseHead(std::string_view(data).substr(0, head_end), length);
                if (!request) { respond({400, "text/plain", "bad request"}); return; }
                if (length > body_limit) { respond({413, "text/plain", "too large"}); return; }
            } else if (data.size() > head_limit) { respond({400, "text/plain", "bad request"}); return; }
        }
        if (head_end != std::string::npos && data.size() >= head_end + 4 + length) break;
        if (!readable(client)) continue;
        const int got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) return;
        data.append(buffer, static_cast<size_t>(got));
    }
    request->body = data.substr(head_end + 4, length);
    Response response;
    try { response = handler_(*request, stopping_); }
    catch (...) { response = {503, "text/plain", "error"}; }
    respond(response);
}

Address::Kind classify(uint32_t ip, std::wstring_view adapter) {
    if (adapter.find(L"Tailscale") != std::wstring_view::npos || (ip & 0xFFC00000u) == 0x64400000u) return Address::Kind::tailscale; // 100.64.0.0/10
    if ((ip & 0xFF000000u) == 0x0A000000u || (ip & 0xFFF00000u) == 0xAC100000u || (ip & 0xFFFF0000u) == 0xC0A80000u) return Address::Kind::local;
    return Address::Kind::global;
}

std::vector<Address> addresses() {
    std::vector<Address> out;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) return out;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const std::wstring name = std::wstring(adapter->FriendlyName ? adapter->FriendlyName : L"") + L" " + (adapter->Description ? adapter->Description : L"");
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            if (!address || address->sin_family != AF_INET) continue;
            const uint32_t ip = ntohl(address->sin_addr.s_addr);
            if ((ip & 0xFF000000u) == 0x7F000000u || (ip & 0xFFFF0000u) == 0xA9FE0000u || ip == 0) continue; // Loopback, link-local.
            wchar_t text[INET_ADDRSTRLEN]{};
            if (!InetNtopW(AF_INET, &address->sin_addr, text, INET_ADDRSTRLEN)) continue;
            out.push_back({classify(ip, name), text});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Address& a, const Address& b) { return a.kind < b.kind; });
    return out;
}

std::string newKey() {
    static constexpr char alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
    std::random_device random; // rand_s on MSVC: cryptographic.
    std::string key;
    for (int i = 0; i < 12; ++i) key += alphabet[random() % (sizeof(alphabet) - 1)];
    return key;
}
} // namespace remote
