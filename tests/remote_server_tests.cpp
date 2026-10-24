// Phone access's server (src/net/remote_server.hpp): request parsing, address kinds and a real
// request over loopback.
#include "../src/net/remote_server.hpp"
#include "../src/net/http.hpp"
#include <iostream>

int main() {
    using remote::Address;
    if (remote::decode("a%20b+c%2Fd%zz%4") != "a b c/d%zz%4") return 1;
    size_t length = 0;
    auto request = remote::parseHead("POST /api/send?k=abc&look=yuki%2D1&empty HTTP/1.1\r\nHost: x\r\ncontent-length: 5", length);
    if (!request || request->method != "POST" || request->path != "/api/send" || request->param("k") != "abc" ||
        request->param("look") != "yuki-1" || request->query.count("empty") != 1 || length != 5) return 2;
    if (remote::parseHead("GET nothing HTTP/1.1", length) || remote::parseHead("garbage", length) ||
        remote::parseHead("POST / HTTP/1.1\r\nContent-Length: 99999999", length)) return 3;
    if (remote::classify(0xC0A8010Au, L"Ethernet") != Address::Kind::local || remote::classify(0x0A000001u, L"") != Address::Kind::local ||
        remote::classify(0xAC1F0001u, L"") != Address::Kind::local || remote::classify(0x64656667u, L"") != Address::Kind::tailscale ||
        remote::classify(0x08080808u, L"Tailscale Tunnel") != Address::Kind::tailscale || remote::classify(0x08080808u, L"Wi-Fi") != Address::Kind::global) return 4;
    const auto key = remote::newKey();
    if (key.size() != 12 || key.find_first_not_of("abcdefghijkmnpqrstuvwxyz23456789") != std::string::npos || key == remote::newKey()) return 5;

    remote::Server server;
    uint16_t port = 0;
    for (uint16_t candidate = 46969; candidate < 46999 && !port; ++candidate)
        if (server.start(candidate, [](const remote::Request& request, const std::atomic_bool&) {
                return remote::Response{200, "text/plain", request.method + " " + request.path + " " + request.param("k") + " " + request.body};
            }, true)) port = candidate;
    if (!port || !server.running()) return 6;
    remote::Server taken;
    if (taken.start(port, {}, true)) return 7; // The same port twice.
    const auto base = L"http://127.0.0.1:" + std::to_wstring(port);
    auto response = httpLocal(base + L"/api/state?k=x%20y", L"", nullptr, 4096, 3000);
    if (response.status != 200 || std::string(response.body.begin(), response.body.end()) != "GET /api/state x y ") return 8;
    const std::string body = "zażółć";
    response = httpLocal(base + L"/api/send", L"Content-Type: text/plain\r\n", &body, 4096, 3000);
    if (response.status != 200 || std::string(response.body.begin(), response.body.end()) != "POST /api/send  " + body) return 9;
    server.stop();
    if (server.running()) return 10;
    std::cout << "Remote server parsing, addresses and loopback requests passed\n";
}
