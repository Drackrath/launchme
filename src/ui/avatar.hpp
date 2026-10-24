#pragma once
// The companion's PNGtuber rig: her pictures (one per emotion, res/companion/<key>/costume_<NN>/) are swapped
// with a pop, and the picture moves in two layers (companion_renderer.cpp): the body breathes and
// sways, the head above the neck tilts, nods on spoken syllables and looks around. Optional
// open-mouth and closed-eyes frames make her talk and blink. Header-only and window-free, so tests
// step it deterministically.
#include "../net/companion.hpp"
#include "spring.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <random>
#include <string_view>

namespace avatar {
using companion::Emotion;
using companion::Look;

// One frame. The picture stands on its bottom edge: x/y move it (y < 0 = up), the scales and the
// body tilt (degrees) apply about the middle of that edge; the head turns about the neck.
struct Pose {
    Emotion emotion = companion::calm, previous = companion::calm;
    float blend = 1; // How much of `emotion` is shown over `previous` (a quick cross-fade).
    float emotion_age = 100; // Seconds since she showed this feeling (her anime marks pop in and fade).
    float x = 0, y = 0, scale_x = 1, scale_y = 1, tilt = 0;
    float head_tilt = 0, head_nod = 0, head_x = 0; // Degrees, units down, units sideways.
    float talk = 0;     // 0..1 while speaking.
    bool mouth = false; // The open-mouth frame (when the look has one).
    bool blink = false; // The closed-eyes frame (when the look has one).
    float time = 0;
};

class Rig {
public:
    explicit Rig(uint32_t seed = 7) : random_(seed) { next_idle_ = uniform(5.0f, 9.0f); }

    Emotion emotion() const { return emotion_; }
    float time() const { return time_; }
    bool talking() const { return talk_age_ < 0.2f; }

    // A new feeling: the picture swaps with a hop and a squash, as a PNGtuber's does.
    void emote(Emotion emotion, bool react = true) {
        revert_ = 0;
        if (emotion == emotion_) { if (react) { hop_.velocity -= 60; emotion_age_ = 0; } return; }
        previous_ = emotion_; emotion_ = emotion; blend_ = 0; emotion_age_ = 0;
        if (!react) { blend_ = 1; return; }
        hop_.velocity -= 170;
        squash_.velocity += 3.0f;
        // Some feelings lean: shy and sleepy tilt the head, shock jumps back up straight.
        if (emotion == Emotion::shy || emotion == Emotion::sleepy || emotion == Emotion::playful || emotion == Emotion::love) tilt_target_ = uniform(0, 1) < 0.5f ? -5.0f : 5.0f;
        else if (emotion == Emotion::shock || emotion == Emotion::fear) { tilt_target_ = 0; hop_.velocity -= 80; }
        else if (emotion == Emotion::thinking || emotion == Emotion::confused) tilt_target_ = 4;
        else if (emotion == Emotion::laugh || emotion == Emotion::wave) { tilt_target_ = 0; hop_.velocity -= 50; }
        else tilt_target_ = 0;
        tilt_hold_ = 2.5f;
    }
    // A passing feeling (a start, a flinch): shown for `seconds`, then back to the calm face.
    void flash(Emotion emotion, float seconds) { flash(emotion, seconds, companion::calm); }
    // The same, then to `then` (her first reaction to a message, then thinking about the answer).
    void flash(Emotion emotion, float seconds, Emotion then) { emote(emotion); revert_ = seconds; revert_to_ = then; }
    // A character just appeared in her speech: each new syllable hops a little, and the picture
    // pulses while she talks.
    void speak(wchar_t ch) {
        talk_age_ = 0;
        const bool vowel = isVowel(ch);
        if (vowel && !last_vowel_) { hop_.velocity -= uniform(16, 30); nod_.velocity += uniform(28, 48); }
        talk_target_ = vowel ? 1.0f : std::iswalpha(ch) ? 0.45f : 0.0f;
        last_vowel_ = vowel;
    }
    // Her voice playing (ElevenLabs): its loudness opens her mouth and each onset moves her like a syllable.
    void voice(float level) {
        talk_age_ = 0;
        talk_target_ = std::clamp(level, 0.0f, 1.0f);
        const bool onset = level > 0.35f;
        if (onset && !last_vowel_) { hop_.velocity -= uniform(16, 30); nod_.velocity += uniform(28, 48); }
        last_vowel_ = onset;
    }
    void silence() { talk_age_ = 1; talk_target_ = 0; }

    void step(float dt, bool idle = true) {
        dt = std::clamp(dt, 0.0f, 0.1f);
        time_ += dt; talk_age_ += dt; emotion_age_ += dt;
        if (talk_age_ > 0.14f) talk_target_ = 0;
        talk_.step(talk_target_, dt, 30, 0.8f);
        hop_.step(0, dt, 17, 0.4f);
        nod_.step(nod_target_, dt, 16, 0.45f);
        look_.step(look_target_, dt, 5, 0.8f);
        // Blinks: 2–6 s apart, sometimes twice in a row (shown when the look has the frame).
        if (blink_ >= 0) { if ((blink_ += dt) > 0.13f) { blink_ = -1; next_blink_ = double_blink_ ? 0.16f : uniform(2.0f, 5.8f); double_blink_ = !double_blink_ && uniform(0, 1) < 0.2f; } }
        else if ((next_blink_ -= dt) <= 0) blink_ = 0;
        squash_.step(0, dt, 16, 0.36f);
        blend_ = std::min(1.0f, blend_ + dt / 0.14f);
        if (tilt_hold_ > 0 && (tilt_hold_ -= dt) <= 0) tilt_target_ = 0;
        tilt_.step(tilt_target_, dt, 7, 0.75f);
        // Now and then, while listening: a curious tilt, a little double hop or a stretch upwards.
        if (idle && !talking() && (next_idle_ -= dt) <= 0) {
            next_idle_ = uniform(6.0f, 12.0f);
            const float roll = uniform(0, 1);
            if (roll < 0.35f) { tilt_target_ = uniform(0, 1) < 0.5f ? -7.0f : 7.0f; tilt_hold_ = uniform(1.0f, 2.0f); }
            else if (roll < 0.55f) { look_target_ = uniform(0, 1) < 0.5f ? -1.0f : 1.0f; look_hold_ = uniform(1.2f, 2.2f); }
            else if (roll < 0.7f) { nod_.velocity += 70; double_nod_ = 0.3f; }
            else if (roll < 0.88f) { hop_.velocity -= 110; double_hop_ = 0.28f; }
            else { squash_.velocity -= 2.2f; }
        }
        if (double_hop_ > 0 && (double_hop_ -= dt) <= 0) hop_.velocity -= 90;
        if (double_nod_ > 0 && (double_nod_ -= dt) <= 0) nod_.velocity += 60;
        if (look_hold_ > 0 && (look_hold_ -= dt) <= 0) look_target_ = 0;
        if (revert_ > 0 && (revert_ -= dt) <= 0) emote(revert_to_);
    }

    Pose pose() const {
        Pose p;
        p.time = time_;
        p.emotion = emotion_; p.previous = previous_; p.blend = blend_; p.emotion_age = emotion_age_;
        const float t = time_;
        const float talk = std::clamp(talk_.value, 0.0f, 1.0f);
        p.talk = talk;
        p.x = std::sin(t * 0.63f) * 2.0f;
        p.y = hop_.value - talk * 3.0f;
        const float squash = std::clamp(squash_.value, -0.25f, 0.25f) * 0.06f;
        p.scale_y = 1 + 0.009f * std::sin(t * 1.75f) + squash + talk * 0.012f;
        p.scale_x = 1 - squash * 0.7f - talk * 0.004f;
        p.tilt = 0.6f * std::sin(t * 0.47f);
        // The head: an idle sway, held tilts, nods on syllables and a glance to the side.
        p.head_tilt = 1.6f * std::sin(t * 0.71f + 0.8f) + tilt_.value + look_.value * 2.0f;
        p.head_nod = std::clamp(nod_.value, -4.0f, 9.0f) + talk * 1.5f;
        p.head_x = look_.value * 4.0f;
        p.mouth = talk > 0.45f;
        p.blink = blink_ >= 0;
        return p;
    }

    static bool isVowel(wchar_t ch) {
        static constexpr std::wstring_view vowels = L"aeiouyąęóAEIOUYĄĘÓ";
        return vowels.find(ch) != std::wstring_view::npos;
    }

private:
    float uniform(float low, float high) { return std::uniform_real_distribution<float>(low, high)(random_); }

    std::mt19937 random_;
    float time_ = 0;
    Emotion emotion_ = companion::calm, previous_ = companion::calm;
    float blend_ = 1, emotion_age_ = 100;
    float next_idle_ = 8, double_hop_ = 0;
    float talk_age_ = 1, talk_target_ = 0;
    bool last_vowel_ = false;
    float tilt_target_ = 0, tilt_hold_ = 0;
    float nod_target_ = 0, double_nod_ = 0, look_target_ = 0, look_hold_ = 0;
    float blink_ = -1, next_blink_ = 3, revert_ = 0;
    Emotion revert_to_ = companion::calm;
    bool double_blink_ = false;
    Spring talk_, hop_, squash_, tilt_, nod_, look_;
};
} // namespace avatar
