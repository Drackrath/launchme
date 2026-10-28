#pragma once
// Audio's platform half, private to audio*.cpp: audio_windows.cpp (XAudio2) and audio_sdl.cpp
// (SDL3 audio streams on the default output). UI thread only.
#include "wave_format.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace audio_output {

// One source voice: its buffers play back to back. Destroying it stops it at once.
class Voice {
public:
    virtual ~Voice() = default;
    // Queues `size` bytes, which the caller keeps alive until they have played (or the voice is
    // gone). loop: the buffer repeats forever; last: no more buffers follow.
    virtual bool submit(const uint8_t* data, size_t size, bool loop, bool last) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0; // Pauses where it is; start() resumes.
    virtual void setVolume(float volume) = 0;
    virtual uint32_t queued() const = 0; // Buffers not yet played to the end.
    virtual uint64_t played() const = 0; // Frames played since the voice was made.
};

// The output: the default device, following a new default where the system allows.
class Engine {
public:
    // nullptr when there is no output (no device, no audio server).
    static std::unique_ptr<Engine> create();
    virtual ~Engine() = default;
    virtual bool failed() const = 0; // The output is gone: rebuild.
    virtual std::unique_ptr<Voice> voice(const WAVEFORMATEX& format) = 0; // nullptr on failure.
};

}
