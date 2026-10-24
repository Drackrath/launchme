#include "failure_cache.hpp"
#include "../platform/atomic_file.hpp"
#include "../model/key_codec.hpp"
#include <chrono>
#include <cwctype>
#include <mutex>
#include <sstream>

namespace {
// v2 retries misses recorded before shared fuzzy SGDB title matching was added.
constexpr int failure_version = 2;
std::mutex storage_mutex;
std::filesystem::path path(const std::filesystem::path& root, std::string_view key, const wchar_t* suffix) {
    return root / (std::filesystem::path(key_codec::encode(key)).wstring() + suffix);
}
uint64_t fingerprint(std::wstring_view title) {
    uint64_t result = 14695981039346656037ull;
    for (wchar_t ch : title) { result ^= static_cast<uint16_t>(std::towlower(ch)); result *= 1099511628211ull; }
    return result;
}
std::ifstream bounded(const std::filesystem::path& file) {
    std::error_code error;
    if (std::filesystem::file_size(file, error) > 256 || error) return {};
    return std::ifstream(file);
}
uint64_t floor(const std::filesystem::path& root, std::string_view key) {
    auto input = bounded(path(root, key, L".retry"));
    uint64_t result = 0; input >> result; return result;
}
}
uint64_t artwork_failure::now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
uint64_t artwork_failure::blockedUntil(const std::filesystem::path& root, std::string_view key, std::wstring_view title, uint64_t time) {
    std::lock_guard lock(storage_mutex);
    auto input = bounded(path(root, key, L".failure"));
    int version = 0, transient = 0; uint64_t started = 0, until = 0, name = 0;
    if (!(input >> version >> started >> until >> name >> transient) || version != failure_version || name != fingerprint(title) ||
        until <= time || until - time > 24 * 60 * 60 * 1000ull || started > until || started <= floor(root, key)) return 0;
    return until;
}
bool artwork_failure::record(const std::filesystem::path& root, std::string_view key, std::wstring_view title,
                              uint64_t request_started, uint64_t until, bool transient) {
    std::lock_guard lock(storage_mutex);
    // A request started before an explicit retry cannot resurrect a cleared
    // failure after it finishes. This barrier also survives a process restart.
    if (request_started <= floor(root, key)) return true;
    auto previous = bounded(path(root, key, L".failure"));
    int version = 0; uint64_t started = 0;
    if (previous >> version >> started && version == failure_version && started > request_started) return true;
    previous.close(); // Windows cannot atomically replace a file still opened by this reader.
    std::ostringstream output;
    output << failure_version << ' ' << request_started << ' ' << until << ' ' << fingerprint(title) << ' ' << transient << '\n';
    return atomic_file::write(path(root, key, L".failure"), output.str());
}
bool artwork_failure::retry(const std::filesystem::path& root, std::string_view key, uint64_t time) {
    std::lock_guard lock(storage_mutex);
    const auto latest = std::max(time, floor(root, key));
    if (!atomic_file::write(path(root, key, L".retry"), std::to_string(latest))) return false;
    std::error_code error; std::filesystem::remove(path(root, key, L".failure"), error);
    return true;
}
void artwork_failure::clear(const std::filesystem::path& root, std::string_view key) {
    std::lock_guard lock(storage_mutex);
    std::error_code error; std::filesystem::remove(path(root, key, L".failure"), error);
}
