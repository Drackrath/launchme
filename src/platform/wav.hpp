#pragma once
// A RIFF/WAVE file with 16-bit PCM (ElevenLabs' wav_24000) as an AudioClip for Audio; nullopt for
// anything else (compressed formats, other sample sizes, truncated files).
#include "flac.hpp"
#include "wave_format.hpp"

#include <cstring>
#include <optional>
#include <span>

inline std::optional<AudioClip> parseWav(std::span<const uint8_t> bytes) {
    const auto u16 = [&](size_t at) { return static_cast<uint16_t>(bytes[at] | bytes[at + 1] << 8); };
    const auto u32 = [&](size_t at) { return static_cast<uint32_t>(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24); };
    const auto tag = [&](size_t at, const char* name) { return std::memcmp(bytes.data() + at, name, 4) == 0; };
    if (bytes.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE")) return std::nullopt;
    std::optional<WAVEFORMATEX> format;
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const uint32_t size = u32(at + 4);
        const size_t body = at + 8;
        if (size > bytes.size() - body) {
            if (!tag(at, "data")) return std::nullopt;
        }
        const size_t available = std::min<size_t>(size, bytes.size() - body); // A streamed header may overstate "data".
        if (tag(at, "fmt ") && available >= 16) {
            const uint16_t kind = u16(body), channels = u16(body + 2), bits = u16(body + 14);
            const uint32_t rate = u32(body + 4);
            if ((kind != WAVE_FORMAT_PCM && kind != WAVE_FORMAT_EXTENSIBLE) || bits != 16 || !channels || channels > 2 || !rate) return std::nullopt;
            WAVEFORMATEX wave{};
            wave.wFormatTag = WAVE_FORMAT_PCM; wave.nChannels = channels; wave.nSamplesPerSec = rate; wave.wBitsPerSample = 16;
            wave.nBlockAlign = static_cast<uint16_t>(channels * 2); wave.nAvgBytesPerSec = rate * wave.nBlockAlign;
            format = wave;
        } else if (tag(at, "data")) {
            if (!format) return std::nullopt;
            AudioClip clip;
            clip.format.resize(sizeof(WAVEFORMATEX));
            std::memcpy(clip.format.data(), &*format, sizeof(WAVEFORMATEX));
            const size_t usable = available - available % format->nBlockAlign;
            clip.samples.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body), bytes.begin() + static_cast<std::ptrdiff_t>(body + usable));
            if (clip.samples.empty()) return std::nullopt;
            return clip;
        }
        at = body + available + (available & 1); // Chunks are word-aligned.
    }
    return std::nullopt;
}
