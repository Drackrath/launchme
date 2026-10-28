#pragma once
// WAVEFORMATEX, the format header every AudioClip carries (flac.hpp): <mmreg.h> on Windows, the
// same 18-byte packed layout elsewhere.
#ifdef _WIN32
#include <Windows.h>
#include <mmreg.h>
#else
#include <cstdint>

#pragma pack(push, 1)
struct WAVEFORMATEX {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};
#pragma pack(pop)
static_assert(sizeof(WAVEFORMATEX) == 18);
inline constexpr uint16_t WAVE_FORMAT_PCM = 1;
inline constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 3;
inline constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;
#endif
