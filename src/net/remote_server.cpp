#include "remote_server.hpp"
#include "../platform/ascii_text.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include "../platform/text_encoding.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <random>

namespace remote {
namespace {
constexpr size_t head_limit = 16 * 1024, body_limit = 64 * 1024;
constexpr int connection_limit = 12;
constexpr auto read_budget = std::chrono::seconds(8);
#ifdef _WIN32
using Socket = SOCKET;
const Socket no_socket = INVALID_SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket no_socket = -1;
void closeSocket(Socket socket) { ::close(socket); }
#endif

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
bool readable(Socket socket) {
#ifdef _WIN32
    fd_set set; FD_ZERO(&set); FD_SET(socket, &set);
    timeval wait{0, 100 * 1000};
    return select(0, &set, nullptr, nullptr, &wait) == 1;
#else
    pollfd item{socket, POLLIN, 0}; // poll, not select: descriptors may exceed FD_SETSIZE.
    return poll(&item, 1, 100) == 1;
#endif
}
bool sendAll(Socket socket, std::string_view data) {
#ifdef _WIN32
    constexpr int flags = 0;
#else
    constexpr int flags = MSG_NOSIGNAL; // A phone that hung up is no SIGPIPE.
#endif
    while (!data.empty()) {
        const int sent = static_cast<int>(send(socket, data.data(), static_cast<int>(std::min<size_t>(data.size(), 64 * 1024)), flags));
        if (sent <= 0) return false;
        data.remove_prefix(static_cast<size_t>(sent));
    }
    return true;
}
std::string lower(std::string_view text) { return ascii_text::lower(text); }
std::string_view trim(std::string_view text) { return ascii_text::trim(text, " \t\r"); }
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
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsock_ = true;
    const Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == no_socket) { stop(); return false; }
    BOOL exclusive = TRUE; // Nobody else may take the same port beside it.
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    // Not inherited by games started meanwhile. Linux never shares a listening port without
    // SO_REUSEPORT; SO_REUSEADDR only lets a restart bind past the old connections' TIME_WAIT.
    const Socket listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listener == no_socket) { stop(); return false; }
    const int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 16) != 0) {
        closeSocket(listener);
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
    if (listener_ != ~uintptr_t{0}) { closeSocket(static_cast<Socket>(listener_)); listener_ = ~uintptr_t{0}; }
#ifdef _WIN32
    if (winsock_) { WSACleanup(); winsock_ = false; }
#endif
    handler_ = {};
    port_ = 0;
}

void Server::run() {
    const auto listener = static_cast<Socket>(listener_);
    while (!stopping_) {
        if (!readable(listener)) continue;
#ifdef _WIN32
        const Socket client = accept(listener, nullptr, nullptr);
#else
        const Socket client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
#endif
        if (client == no_socket) continue;
        {
            std::lock_guard lock(mutex_);
            if (active_ >= connection_limit) { closeSocket(client); continue; }
            ++active_;
        }
        std::thread([this, client] {
            serve(static_cast<uintptr_t>(client));
            closeSocket(client);
            std::lock_guard lock(mutex_);
            --active_;
            idle_.notify_all();
        }).detach(); // stop() waits for every one of them.
    }
}

void Server::serve(uintptr_t handle) {
    const auto client = static_cast<Socket>(handle);
#ifdef _WIN32
    const DWORD send_timeout = 10000;
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout), sizeof(send_timeout));
    constexpr int send_done = SD_SEND;
#else
    const timeval send_timeout{10, 0};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    constexpr int send_done = SHUT_WR;
#endif
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
        shutdown(client, send_done);
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
        const int got = static_cast<int>(recv(client, buffer, sizeof(buffer), 0));
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
    // "tailscale0": the Linux interface.
    if (adapter.find(L"Tailscale") != std::wstring_view::npos || adapter.starts_with(L"tailscale") || (ip & 0xFFC00000u) == 0x64400000u) return Address::Kind::tailscale; // 100.64.0.0/10
    if ((ip & 0xFF000000u) == 0x0A000000u || (ip & 0xFFF00000u) == 0xAC100000u || (ip & 0xFFFF0000u) == 0xC0A80000u) return Address::Kind::local;
    return Address::Kind::global;
}

std::vector<Address> addresses() {
    std::vector<Address> out;
#ifdef _WIN32
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
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (const auto* item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || (item->ifa_flags & IFF_LOOPBACK) ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_RUNNING)) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(item->ifa_addr);
        const uint32_t ip = ntohl(address->sin_addr.s_addr);
        if ((ip & 0xFF000000u) == 0x7F000000u || (ip & 0xFFFF0000u) == 0xA9FE0000u || ip == 0) continue; // Loopback, link-local.
        char text[INET_ADDRSTRLEN]{};
        if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) continue;
        out.push_back({classify(ip, text_encoding::wideLenient(item->ifa_name ? item->ifa_name : "")), text_encoding::wideLenient(text)});
    }
    freeifaddrs(list);
#endif
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
