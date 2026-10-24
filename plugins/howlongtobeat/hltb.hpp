#pragma once
#include "../../src/plugins/json.hpp"
#include "../../src/net/http.hpp"
#include <functional>

namespace hltb {
struct Game {
    uint32_t id = 0;
    std::string name;
    uint32_t main = 0, extra = 0, complete = 0;
};
struct Result { std::vector<Game> games; bool valid = false; std::string error; };
Result parseResults(std::string_view bytes);
Result parseGamePage(std::string_view html);
std::string duration(uint32_t seconds, bool pl);
std::string endpoint(std::string_view script);
json::Value gameJson(const Game& game);
class Client {
public:
    std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)> get = httpGet;
    std::function<HttpResponse(const std::wstring&, const std::wstring&, const std::string&, size_t)> post = httpPostPrivate;
    Result search(const std::string& name);
    Result game(uint32_t id);
private:
    std::string endpoint_;
    bool discover();
};
}
