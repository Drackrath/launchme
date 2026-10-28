// Audio on its SDL3 output (src/platform/audio_sdl.cpp), on SDL's dummy driver: nothing is heard.
// Cues, looping music with fades, and her voice clips queued back to back with their level and speaker.
#include "../src/platform/audio.hpp"
#include "../src/platform/wav.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <thread>

namespace {
// A 16-bit mono tone as a WAV file, through parseWav like ElevenLabs' clips.
AudioClip tone(uint32_t rate, double seconds, double amplitude) {
    const uint32_t frames = static_cast<uint32_t>(rate * seconds), data = frames * 2;
    std::vector<uint8_t> bytes;
    const auto text = [&](const char* tag) { bytes.insert(bytes.end(), tag, tag + 4); };
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    const auto u16 = [&](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    text("RIFF"); u32(36 + data); text("WAVE");
    text("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    text("data"); u32(data);
    for (uint32_t i = 0; i < frames; ++i)
        u16(static_cast<uint16_t>(static_cast<int16_t>(amplitude * 32767 * std::sin(2 * std::numbers::pi * 440.0 * i / rate))));
    return parseWav(bytes).value_or(AudioClip{});
}

template <class Done> bool pump(Audio& audio, Done done, std::chrono::milliseconds limit) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        audio.update(0.01f);
    }
    return true;
}
}

int main() {
    setenv("SDL_AUDIO_DRIVER", "dummy", 1); // Never the real output.
    using namespace std::chrono_literals;
    Audio audio;
    const auto cue = tone(48000, 1.0, 0.5);
    if (cue.samples.size() != 96000) return 1;
    audio.setCue(SoundCue::navigation, cue);
    audio.setCue(SoundCue::toast, cue);
    audio.navigate(); audio.toast(); audio.launch(); audio.context();
    audio.setMusic(tone(44100, 0.5, 0.3)); // Loops; a second theme waits for the first to fade out.
    audio.setMusicLevel(0.2f);
    for (int i = 0; i < 20; ++i) { audio.update(0.05f); std::this_thread::sleep_for(5ms); }
    audio.setMusic(tone(22050, 0.5, 0.3));
    audio.stopMusic();
    audio.update(0.1f);
    audio.resumeMusic();

    // Two sentences back to back: speaking while either is queued, the second's speaker after the first.
    if (audio.speaking()) return 2;
    audio.speak(tone(24000, 0.3, 0.8), 1);
    audio.speak(tone(24000, 0.3, 0.8), 2);
    if (!audio.speaking() || audio.speakingSpeaker() != 1) return 3;
    float loudest = 0;
    bool second = false;
    if (!pump(audio, [&] {
            loudest = std::max(loudest, audio.voiceLevel());
            second = second || audio.speakingSpeaker() == 2;
            return !audio.speaking();
        }, 3s)) return 4;
    if (loudest <= 0.5f || !second) { std::cerr << "level=" << loudest << " second=" << second << '\n'; return 5; }
    audio.update(0.01f);
    if (audio.voiceLevel() != 0 || audio.speakingSpeaker() != -1) return 6;

    audio.speak(tone(24000, 2.0, 0.8));
    audio.stopSpeaking(); // Cut off at once.
    if (audio.speaking()) return 7;
    audio.recover(); // A working output is kept.
    audio.clearMusic();
    std::cout << "Cues, music fades and queued voice clips on the SDL output passed\n";
}
