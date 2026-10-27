#include "hashing.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/evp.h>
#endif

#include <algorithm>
#include <fstream>

namespace hashing {
#ifdef _WIN32
struct Digest::State {
    BCRYPT_HASH_HANDLE hash = nullptr;
    ULONG size = 0;
    bool ok = false;
};
Digest::Digest(Algorithm algorithm) : state_(std::make_unique<State>()) {
    // The pseudo-handles need no provider to open or close.
    const auto provider = algorithm == Algorithm::md5 ? BCRYPT_MD5_ALG_HANDLE : BCRYPT_SHA256_ALG_HANDLE;
    state_->size = algorithm == Algorithm::md5 ? 16 : 32;
    state_->ok = BCRYPT_SUCCESS(BCryptCreateHash(provider, &state_->hash, nullptr, 0, nullptr, 0, 0));
}
Digest::~Digest() { if (state_->hash) BCryptDestroyHash(state_->hash); }
bool Digest::update(const void* data, size_t size) {
    while (state_->ok && size) {
        const auto chunk = static_cast<ULONG>(std::min<size_t>(size, 1u << 30));
        state_->ok = BCRYPT_SUCCESS(BCryptHashData(state_->hash, static_cast<PUCHAR>(const_cast<void*>(data)), chunk, 0));
        data = static_cast<const uint8_t*>(data) + chunk; size -= chunk;
    }
    return state_->ok;
}
std::optional<std::vector<uint8_t>> Digest::finish() {
    if (!state_->ok) return std::nullopt;
    std::vector<uint8_t> digest(state_->size);
    const bool finished = BCRYPT_SUCCESS(BCryptFinishHash(state_->hash, digest.data(), state_->size, 0));
    state_->ok = false; // Finished: no further updates.
    return finished ? std::optional(std::move(digest)) : std::nullopt;
}
#else
struct Digest::State {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    bool ok = false;
};
Digest::Digest(Algorithm algorithm) : state_(std::make_unique<State>()) {
    state_->ok = state_->context &&
        EVP_DigestInit_ex(state_->context, algorithm == Algorithm::md5 ? EVP_md5() : EVP_sha256(), nullptr) == 1;
}
Digest::~Digest() { EVP_MD_CTX_free(state_->context); }
bool Digest::update(const void* data, size_t size) {
    state_->ok = state_->ok && EVP_DigestUpdate(state_->context, data, size) == 1;
    return state_->ok;
}
std::optional<std::vector<uint8_t>> Digest::finish() {
    if (!state_->ok) return std::nullopt;
    std::vector<uint8_t> digest(EVP_MAX_MD_SIZE);
    unsigned size = 0;
    state_->ok = false; // Finished: no further updates.
    if (EVP_DigestFinal_ex(state_->context, digest.data(), &size) != 1) return std::nullopt;
    digest.resize(size);
    return digest;
}
#endif

std::string hex(std::span<const uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

std::string bytesHex(Algorithm algorithm, std::span<const uint8_t> bytes) {
    Digest digest(algorithm);
    if (!digest.update(bytes.data(), bytes.size())) return {};
    const auto value = digest.finish();
    return value ? hex(*value) : std::string{};
}

std::string fileHex(Algorithm algorithm, const std::filesystem::path& file, const std::function<bool()>& cancel) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    Digest digest(algorithm);
    std::vector<char> buffer(256 * 1024);
    while (input) {
        if (cancel && cancel()) return {};
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() && !digest.update(buffer.data(), static_cast<size_t>(input.gcount()))) return {};
    }
    if (!input.eof() || (cancel && cancel())) return {};
    const auto value = digest.finish();
    return value ? hex(*value) : std::string{};
}
}
