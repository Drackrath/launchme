#pragma once
#include "image_decoder.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

// Block-compressed artwork kept on disk (AppSettings::texture_compression, src/ui/image_decoder.cpp):
// a decode that ends in BC1/BC3 blocks is written to <folder>/<two hex digits>/<key>.tex, keyed by the
// encoded picture (its hash and size) and by everything the result depends on (DecodeKind). The next
// decode of the same bytes for the same use reads the blocks instead of decoding and compressing
// again: a fraction of a millisecond instead of ~5–15 ms for a poster. Files are written atomically
// (a temporary, then a rename) and touched when read; past the budget the least recently used go
// (checked every 64 writes). Anything unreadable is a miss. Thread-safe: the decoder's workers share one.
class TextureCache {
public:
    TextureCache(std::filesystem::path folder, uint64_t budget_bytes);
    // The key of `bytes` decoded with `kind`; the same for the same picture read again from disk.
    static uint64_t key(const std::vector<uint8_t>& bytes, const DecodeKind& kind);
    // The blocks, size, accent and ink bounds stored for `key`; false when there are none.
    bool load(uint64_t key, DecodedImage& image) const;
    // A block-compressed result (anything else is ignored).
    void store(uint64_t key, const DecodedImage& image);
    const std::filesystem::path& folder() const { return folder_; }

private:
    std::filesystem::path file(uint64_t key) const;
    void trim();
    std::filesystem::path folder_;
    uint64_t budget_;
    std::atomic<unsigned> writes_{0};
    std::mutex trim_mutex_;
};
