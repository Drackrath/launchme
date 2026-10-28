#pragma once
// Settings → Asystent → "Dostęp z telefonu": a tiny HTTP/1.1 server for her page in a phone's
// browser (src/app/companion_remote.cpp). Every interface (LAN, Tailscale), one request per
// connection, a thread per connection (at most a few); no TLS — Tailscale encrypts its tunnel.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace remote {
struct Request {
    std::string method, path, body;
    std::map<std::string, std::string, std::less<>> query;
    std::string param(std::string_view name) const;
};
struct Response {
    int status = 200;
    std::string type = "application/json; charset=utf-8";
    std::string body;
    std::string cache = "no-store";
};
// Called on the connection's thread; `stopping` turns true when the server stops.
using Handler = std::function<Response(const Request&, const std::atomic_bool& stopping)>;

class Server {
public:
    Server() = default;
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    // false: the port is taken (or Winsock/the socket API failed). Tests listen on 127.0.0.1 only (no firewall prompt).
    bool start(uint16_t port, Handler handler, bool loopback_only = false);
    void stop();                                // Waits for the connections (each polls `stopping`).
    bool running() const { return thread_.joinable(); }
    uint16_t port() const { return port_; }

private:
    void run();
    void serve(uintptr_t client);
    uintptr_t listener_ = ~uintptr_t{0};
    uint16_t port_ = 0;
#ifdef _WIN32
    bool winsock_ = false;
#endif
    Handler handler_;
    std::atomic_bool stopping_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable idle_;
    int active_ = 0;
};

// "%41+b" -> "A b".
std::string decode(std::string_view text);
// The request line and headers (without the blank line); nullopt: not HTTP. `length`: Content-Length.
std::optional<Request> parseHead(std::string_view head, size_t& length);

// This computer's IPv4 addresses that a phone can reach.
struct Address {
    enum class Kind { local, tailscale, global } kind = Kind::local;
    std::wstring ip;
};
std::vector<Address> addresses();
Address::Kind classify(uint32_t ipv4_host_order, std::wstring_view adapter);
// A random key for the page's address (a-z, 2-9; no 0/o, 1/l).
std::string newKey();
} // namespace remote
