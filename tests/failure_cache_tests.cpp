#include "../src/artwork/failure_cache.hpp"
#include "../src/model/key_codec.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-failure-cache-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const auto key = "gog:../opaque-id:0";
    if (!artwork_failure::record(root, key, L"Title", 1000, 10000, false)) return 1;
    if (artwork_failure::blockedUntil(root, key, L"TITLE", 2000) != 10000) return 2;
    if (artwork_failure::blockedUntil(root, key, L"Resolved title", 2000) || artwork_failure::blockedUntil(root, "steam:123:0", L"Title", 2000)) return 3;
    if (artwork_failure::blockedUntil(root, key, L"Title", 10001)) return 4;
    if (!artwork_failure::retry(root, key, 3000) || artwork_failure::blockedUntil(root, key, L"Title", 3001)) return 5;
    // A slow old request finishing after Retry must not restore its failure.
    if (!artwork_failure::record(root, key, L"Title", 1000, 15000, true) || artwork_failure::blockedUntil(root, key, L"Title", 4000)) return 6;
    if (!artwork_failure::record(root, key, L"Title", 4001, 16000, true) || artwork_failure::blockedUntil(root, key, L"Title", 5000) != 16000) return 7;
    if (!artwork_failure::record(root, key, L"Title", 3500, 12000, false) || artwork_failure::blockedUntil(root, key, L"Title", 5000) != 16000) return 8;
    if (!artwork_failure::record(root, key, L"Title", 5001, 22000, true) || artwork_failure::blockedUntil(root, key, L"Title", 6000) != 22000) return 10;
    // A pre-fuzzy negative lookup must not suppress the new title resolver for a day.
    {
        std::fstream legacy(root / (key_codec::encode(key) + ".failure"), std::ios::in | std::ios::out | std::ios::binary);
        legacy.put('1');
    }
    if (artwork_failure::blockedUntil(root, key, L"Title", 6000)) return 11;
    artwork_failure::clear(root, key);
    if (artwork_failure::blockedUntil(root, key, L"Title", 5000)) return 9;
    std::cout << "Persistent failure TTL, identity, title changes and retry races passed\n";
}
