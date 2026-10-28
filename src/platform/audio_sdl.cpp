// Audio's output on Linux: one SDL3 logical device on the default output (SDL follows a new
// default, PipeWire or PulseAudio), one audio stream bound to it per voice (audio_output.hpp).
#include "audio_output.hpp"

#include <SDL3/SDL.h>

#include <deque>

namespace audio_output {
namespace {
// The stream pulls from the caller's buffers on SDL's audio thread (get callback, stream locked):
// nothing is copied up front, and a looping buffer never ends. SDL copies what it takes.
class SdlVoice final : public Voice {
public:
    SdlVoice(SDL_AudioDeviceID device, SDL_AudioStream* stream, uint32_t block) : device_(device), stream_(stream), block_(block) {
        SDL_SetAudioStreamGetCallback(stream_, &SdlVoice::feed, this);
    }
    ~SdlVoice() override { SDL_DestroyAudioStream(stream_); }
    bool submit(const uint8_t* data, size_t size, bool loop, bool) override {
        size -= size % block_;
        if (!data || !size) return false;
        Lock lock(stream_);
        // Buffers that have played out are forgotten; their memory may go.
        const uint64_t heard = playedBytes();
        while (next_ && buffers_.front().end <= heard) { buffers_.pop_front(); --next_; }
        buffers_.push_back({data, size, loop});
        return true;
    }
    bool start() override {
        if (!bound_) bound_ = SDL_BindAudioStream(device_, stream_);
        return bound_;
    }
    void stop() override { // Unbound, the stream keeps its place.
        if (bound_) SDL_UnbindAudioStream(stream_);
        bound_ = false;
    }
    void setVolume(float volume) override { SDL_SetAudioStreamGain(stream_, volume); }
    uint32_t queued() const override {
        Lock lock(stream_);
        const uint64_t heard = playedBytes();
        uint32_t count = 0;
        for (size_t i = 0; i < buffers_.size(); ++i) count += i >= next_ || buffers_[i].end > heard;
        return count;
    }
    uint64_t played() const override {
        Lock lock(stream_);
        return playedBytes() / block_;
    }
private:
    struct Lock {
        SDL_AudioStream* stream;
        explicit Lock(SDL_AudioStream* s) : stream(s) { SDL_LockAudioStream(stream); }
        ~Lock() { SDL_UnlockAudioStream(stream); }
    };
    struct Buffer {
        const uint8_t* data; size_t size; bool loop;
        uint64_t end = 0; // fed_ once the whole buffer is in the stream.
    };
    // Bytes taken in minus those still waiting in the stream: what the device has been given.
    uint64_t playedBytes() const {
        const int waiting = SDL_GetAudioStreamQueued(stream_);
        return fed_ - std::min<uint64_t>(fed_, waiting > 0 ? static_cast<uint64_t>(waiting) : 0);
    }
    static void SDLCALL feed(void* user, SDL_AudioStream* stream, int needed, int) {
        auto& self = *static_cast<SdlVoice*>(user);
        while (needed > 0 && self.next_ < self.buffers_.size()) {
            auto& buffer = self.buffers_[self.next_];
            const size_t chunk = std::min(static_cast<size_t>(needed), buffer.size - self.offset_);
            if (!SDL_PutAudioStreamData(stream, buffer.data + self.offset_, static_cast<int>(chunk))) return;
            self.offset_ += chunk; self.fed_ += chunk; needed -= static_cast<int>(chunk);
            if (self.offset_ < buffer.size) continue;
            self.offset_ = 0;
            if (buffer.loop) continue;
            buffer.end = self.fed_;
            // The last of the queue: the resampler lets go of what it holds back, so it plays out.
            if (++self.next_ == self.buffers_.size()) SDL_FlushAudioStream(stream);
        }
    }
    SDL_AudioDeviceID device_;
    SDL_AudioStream* stream_;
    uint32_t block_;
    // Guarded by the stream's lock. next_: the buffer being fed; those before it are in the stream.
    std::deque<Buffer> buffers_;
    size_t next_ = 0, offset_ = 0;
    uint64_t fed_ = 0;
    bool bound_ = false;
};

class SdlEngine final : public Engine {
public:
    bool open() {
        SDL_SetHintWithPriority(SDL_HINT_APP_NAME, "Launchme", SDL_HINT_DEFAULT); // The name in the system mixer.
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        initialized_ = true;
        device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        return device_ != 0;
    }
    ~SdlEngine() override {
        if (device_) SDL_CloseAudioDevice(device_);
        if (initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    bool failed() const override {
        SDL_AudioSpec spec{};
        return !SDL_GetAudioDeviceFormat(device_, &spec, nullptr); // Its device is gone and no default replaced it.
    }
    std::unique_ptr<Voice> voice(const WAVEFORMATEX& format) override {
        SDL_AudioSpec spec{};
        const bool pcm = format.wFormatTag == WAVE_FORMAT_PCM || format.wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        if (pcm && format.wBitsPerSample == 16) spec.format = SDL_AUDIO_S16LE;
        else if (pcm && format.wBitsPerSample == 8) spec.format = SDL_AUDIO_U8;
        else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32) spec.format = SDL_AUDIO_F32LE;
        else return nullptr;
        spec.channels = format.nChannels;
        spec.freq = static_cast<int>(format.nSamplesPerSec);
        const uint32_t block = format.nChannels * format.wBitsPerSample / 8u;
        if (!spec.channels || !spec.freq || !block) return nullptr;
        SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
        if (!stream) return nullptr;
        return std::make_unique<SdlVoice>(device_, stream, block);
    }
private:
    bool initialized_ = false;
    SDL_AudioDeviceID device_ = 0;
};
}

std::unique_ptr<Engine> Engine::create() {
    auto engine = std::make_unique<SdlEngine>();
    return engine->open() ? std::move(engine) : nullptr;
}
}
