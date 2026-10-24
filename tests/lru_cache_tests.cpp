#include "../src/model/lru_cache.hpp"

#include <iostream>
#include <string>
#include <unordered_set>

namespace {
struct Blob { size_t size = 0; };
struct BlobBytes { size_t operator()(const Blob& blob) const { return blob.size; } };
using Cache = LruCache<Blob, BlobBytes>;
}

int main() {
    Cache cache(100);
    // Byte accounting follows inserts, in-place edits (resize) and erase.
    cache.at("a").size = 30; cache.resize("a");
    cache.at("b").size = 30; cache.resize("b");
    cache.at("c").size = 30; cache.resize("c");
    if (cache.bytes() != 90 || cache.size() != 3) return 1;
    cache.at("b").size = 10; cache.resize("b");
    if (cache.bytes() != 70) return 2;
    if (!cache.erase("b") || cache.erase("b") || cache.bytes() != 60 || cache.contains("b")) return 3;
    cache.at("b").size = 30; cache.resize("b"); // Order now (oldest first): a, c, b.

    // Under budget nothing is dropped.
    if (!cache.trim([](const std::string&) { return false; }).empty()) return 4;

    // find() marks as used, peek() does not: "a" becomes the most recent.
    if (!cache.find("a") || !cache.peek("c")) return 5;
    // Over budget (120 > 100): least recently used first, down to 7/8 of the budget (87).
    cache.at("d").size = 30; cache.resize("d");
    auto dropped = cache.trim([](const std::string&) { return false; });
    if (dropped.size() != 2 || dropped[0] != "c" || dropped[1] != "b" || cache.bytes() != 60 ||
        !cache.contains("a") || !cache.contains("d")) return 6;

    // Pinned entries (on screen) survive even when they are the oldest.
    cache.at("e").size = 50; cache.resize("e"); // a, d, e = 110.
    const std::unordered_set<std::string> pinned{"a"};
    dropped = cache.trim([&](const std::string& key) { return pinned.contains(key); });
    if (dropped.size() != 1 || dropped[0] != "d" || !cache.contains("a") || cache.bytes() != 80) return 7;

    // Everything pinned: stays over budget rather than dropping visible artwork.
    cache.at("f").size = 40; cache.resize("f");
    if (!cache.trim([](const std::string&) { return true; }).empty() || cache.bytes() != 120) return 8;

    cache.clear();
    if (cache.bytes() || cache.size() || cache.find("a")) return 9;
    std::cout << "LRU byte accounting, recency order, pinning and hysteresis passed\n";
    return 0;
}
