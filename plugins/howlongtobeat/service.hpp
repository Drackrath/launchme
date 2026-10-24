#pragma once
#include "hltb.hpp"
#include "../../src/model/key_codec.hpp"
#include "../../src/platform/atomic_file.hpp"
#include "../../src/platform/text_encoding.hpp"
#include "../../src/library/search.hpp"
#include <charconv>
#include <chrono>
#include <fstream>

namespace hltb {
class Service {
public:
    Client client; // Injectable transport; tests never call the live service.
    json::Value dispatch(const json::Value& input);
private:
    struct Context { std::string key, name; std::optional<uint64_t> played; } page_game_;
    struct Cache { std::vector<Game> matches; uint32_t selected = 0; uint64_t fetched = 0; };
    std::filesystem::path data_;
    std::map<std::string, uint64_t> retry_after_;
    static uint64_t now();
    std::filesystem::path file(const Context& game) const;
    Cache load(const Context& game) const;
    bool save(const Context& game, const Cache& cache) const;
    std::string update(const Context& game, Cache& cache, bool force, bool pl);
    static const Game* selected(const Cache& cache);
    static bool autoMatch(Cache& cache, const std::string& name);
    static json::Value comparison(const Context& game, const Cache& cache, bool pl, std::string status);
    static json::Value page(const Context& game, const Cache& cache, bool pl, const std::string& status);
};
}
