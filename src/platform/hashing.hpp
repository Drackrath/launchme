#pragma once
// Message digests (CNG on Windows, OpenSSL elsewhere): checksums of downloads, ROMs and backups.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hashing {
enum class Algorithm { md5, sha256 };

class Digest {
public:
    explicit Digest(Algorithm algorithm);
    ~Digest();
    Digest(const Digest&) = delete;
    Digest& operator=(const Digest&) = delete;
    // False once anything failed (the digest is then never produced).
    bool update(const void* data, size_t size);
    std::optional<std::vector<uint8_t>> finish();
private:
    struct State;
    std::unique_ptr<State> state_;
};

std::string hex(std::span<const uint8_t> bytes);
// Lowercase hex digests; empty when hashing fails (unreadable file, `cancel` returned true).
std::string bytesHex(Algorithm algorithm, std::span<const uint8_t> bytes);
std::string fileHex(Algorithm algorithm, const std::filesystem::path& file, const std::function<bool()>& cancel = {});
}
