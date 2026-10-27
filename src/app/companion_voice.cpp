// Her voice: ElevenLabs (src/net/elevenlabs.hpp) reads her line in the interface's language while
// her text appears; in a duet each companion speaks with her own voice. The key (Settings or
// ELEVENLABS_API_KEY) is checked once on a worker, which also lists the account's voices. Each
// line is synthesized chunk by chunk on a worker: the first sentence releases her text, later
// chunks queue behind it in Audio, and the loudness of what plays moves the speaker's mouth
// (avatar::Rig::voice). A voice her plan refuses (Voice Library voices on a free account) gives
// way to the next in her chain — her character.txt's free fallback, then a built-in voice.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/wav.hpp"

namespace {
bool english() { return i18n::english(); }
}

bool Launcher::companionVoiceFor(bool chat) const {
    using Voice = AppSettings::CompanionVoice;
    return settings_.companion && (settings_.companion_voice == Voice::everywhere || (chat && settings_.companion_voice == Voice::chat));
}

elevenlabs::Speaker Launcher::companionVoiceSpeaker(companion::Look look) const {
    const auto key = companion::lookKey(look);
    const auto chosen = settings_.companion_voice_ids.find(key);
    return elevenlabs::speaker(companion::character(look), companionPersonaOf(look),
                               chosen == settings_.companion_voice_ids.end() ? std::string_view{} : std::string_view(chosen->second),
                               settings_.companion_voice_model, companion_.voice.voices);
}

std::wstring Launcher::companionVoiceName(companion::Look look) const {
    const auto speaker = companionVoiceSpeaker(look);
    const auto& voice = companion_.voice;
    const auto named = [&](size_t i) { return speaker.names[i].empty() ? text_encoding::wideLenient(speaker.voices[i]) : speaker.names[i]; };
    // The one that spoke last (still hers), else the first her plan has not refused.
    if (const auto spoke = voice.spoke.find(static_cast<int>(look)); spoke != voice.spoke.end())
        for (size_t i = 0; i < speaker.voices.size(); ++i) if (speaker.voices[i] == spoke->second) return named(i);
    for (size_t i = 0; i < speaker.voices.size(); ++i) if (!voice.blocked.contains(speaker.voices[i])) return named(i);
    return {};
}

std::wstring Launcher::companionVoiceProblem() const {
    using elevenlabs::Problem;
    const auto& voice = companion_.voice;
    switch (voice.problem) {
    case Problem::none: return {};
    case Problem::no_key: return tr(L"brak klucza API (Ustawienia → Asystent)");
    case Problem::key_refused: return tr(L"klucz API odrzucony — sprawdź go w Ustawieniach");
    case Problem::permission: return tr(L"klucz bez uprawnienia Text to Speech");
    case Problem::quota: return tr(L"wyczerpany limit znaków w tym miesiącu");
    case Problem::paid_voice: return tr(L"jej głosy wymagają płatnego planu");
    case Problem::busy: return tr(L"za dużo zapytań naraz — spróbuj za chwilę");
    case Problem::offline: return tr(L"brak połączenia");
    case Problem::failed: return voice.status ? std::wstring(L"HTTP ") + std::to_wstring(voice.status) : std::wstring(tr(L"błąd syntezy"));
    }
    return {};
}

void Launcher::requestCompanionVoiceStatus() {
    auto& voice = companion_.voice;
    if (voice.engine == CompanionVoice::Engine::checking) return;
    voice.engine = CompanionVoice::Engine::checking;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionVoiceMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_voice_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, request = companion_.voice_request, key = companion_.voice_key()] {
        auto message = std::make_unique<CompanionVoiceMessage>();
        auto status = elevenlabs::check(request, key);
        message->ok = status.ok;
        message->problem = status.problem;
        message->status = status.status;
        message->voices = std::move(status.voices);
        post(std::move(message));
    });
}

void Launcher::onCompanionVoice(std::unique_ptr<CompanionVoiceMessage> message) {
    auto& voice = companion_.voice;
    using Engine = CompanionVoice::Engine;
    const bool settings_page = settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::companion);
    if (message->kind == CompanionVoiceMessage::Kind::audio) {
        for (auto& refused : message->refused) voice.blocked.insert(std::move(refused));
        if (!message->voice.empty()) voice.spoke[message->speaker] = message->voice;
        if (message->problem != elevenlabs::Problem::none && message->clip.samples.empty()) {
            voice.problem = message->problem; voice.status = message->status;
            // Without a key that works nothing more is tried until it changes; the rest may pass.
            if (message->problem == elevenlabs::Problem::key_refused || message->problem == elevenlabs::Problem::quota ||
                message->problem == elevenlabs::Problem::permission || message->problem == elevenlabs::Problem::no_key) voice.engine = Engine::failed;
        } else if (!message->clip.samples.empty()) { voice.problem = elevenlabs::Problem::none; voice.status = 0; }
        if (message->generation == voice.generation) {
            // The diagnostics: how long her text waited for this first chunk.
            if (voice.pending && !companion_.diagnostics.empty())
                if (auto& last = companion_.diagnostics.back(); last.voiced && !last.voice_ms) last.voice_ms = static_cast<uint32_t>(GetTickCount64() - last.arrived);
            voice.pending = false;
            if (!message->clip.samples.empty()) audio_.speak(std::move(message->clip), message->speaker);
        }
        if (settings_page) buildSettingsRows();
        return;
    }
    voice.voices = std::move(message->voices);
    voice.problem = message->problem;
    voice.status = message->status;
    voice.engine = message->ok ? Engine::ready : message->problem == elevenlabs::Problem::no_key ? Engine::missing : Engine::failed;
    if (settings_page) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionHush() {
    auto& voice = companion_.voice;
    voice.cancel->store(true);
    voice.cancel = std::make_shared<std::atomic_bool>(false);
    ++voice.generation;
    voice.pending = false;
    audio_.stopSpeaking();
}

void Launcher::companionSpeak(const std::wstring& text, companion::Emotion emotion) {
    companion::Line single{companionLook(), text, {{0, emotion}}, {}};
    companionSpeakLines({std::move(single)});
}

void Launcher::companionSpeakLines(const std::vector<companion::Line>& lines) {
    companionHush();
    auto& voice = companion_.voice;
    if (voice.engine != CompanionVoice::Engine::ready) {
        if (voice.engine == CompanionVoice::Engine::unknown) requestCompanionVoiceStatus(); // Next time, then.
        return;
    }
    // Who speaks each message, resolved here (Settings, her character.txt): the worker only talks.
    std::map<int, elevenlabs::Speaker> speakers;
    for (const auto& line : lines)
        if (!speakers.contains(static_cast<int>(line.speaker))) speakers[static_cast<int>(line.speaker)] = companionVoiceSpeaker(line.speaker);
    auto chunks = elevenlabs::chunks(lines, [&](companion::Look look) { return speakers[static_cast<int>(look)]; });
    if (chunks.empty()) return;
    voice.pending = true; voice.waited = 0;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionVoiceMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_voice_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, cancel = voice.cancel, generation = voice.generation, request = companion_.voice_request, key = companion_.voice_key(),
                 blocked = voice.blocked, speakers = std::move(speakers), chunks = std::move(chunks), en = english()]() mutable {
        HttpCancellation cancellation(*cancel);
        for (size_t i = 0; i < chunks.size() && !cancel->load(); ++i) {
            auto message = std::make_unique<CompanionVoiceMessage>();
            message->kind = CompanionVoiceMessage::Kind::audio;
            message->generation = generation;
            message->speaker = static_cast<int>(chunks[i].speaker);
            message->last = i + 1 == chunks.size();
            auto spoken = elevenlabs::speak(request, key, speakers[message->speaker], chunks[i], en, blocked);
            blocked.insert(spoken.refused.begin(), spoken.refused.end());
            if (auto clip = parseWav(spoken.wav)) message->clip = std::move(*clip);
            message->voice = std::move(spoken.voice);
            message->refused = std::move(spoken.refused);
            message->problem = message->clip.samples.empty() && spoken.problem == elevenlabs::Problem::none ? elevenlabs::Problem::failed : spoken.problem;
            message->status = spoken.status;
            const bool failed = message->clip.samples.empty();
            if (!cancel->load()) post(std::move(message));
            if (failed) break; // The service refuses: her text goes on without a voice.
        }
    });
}

void Launcher::companionVoiceTest() {
    if (!settings_.companion) return;
    auto& voice = companion_.voice;
    if (voice.engine == CompanionVoice::Engine::missing || (voice.engine == CompanionVoice::Engine::failed && voice.problem == elevenlabs::Problem::no_key)) {
        showNotification(NotificationKind::error, tr(L"Brak klucza ElevenLabs"), tr(L"Wpisz klucz API w wierszu poniżej albo ustaw ELEVENLABS_API_KEY."));
        return;
    }
    if (voice.engine != CompanionVoice::Engine::ready) {
        requestCompanionVoiceStatus();
        showNotification(NotificationKind::launch, tr(L"Sprawdzanie ElevenLabs…"), tr(L"Spróbuj za chwilę."));
        return;
    }
    const auto sample = elevenlabs::sample(companionPersona(), english());
    companion_.rig.emote(sample.emotion);
    auto parsed = companion::parseText(sample.text); // Its cue ({giggles}) acted, as in her replies.
    companionSpeakLines({{companionLook(), std::move(parsed.text), {{0, sample.emotion}}, {}, std::move(parsed.cues)}});
}

bool Launcher::tickCompanionVoice(float dt) {
    auto& voice = companion_.voice;
    if (voice.pending) voice.waited += dt;
    if (!audio_.speaking()) return voice.pending;
    const float level = audio_.voiceLevel();
    const int speaker = audio_.speakingSpeaker();
    const auto look = speaker >= 0 && speaker < companion::lookCount() ? static_cast<companion::Look>(speaker) : companionLook();
    auto& active = companionRig(look);
    active.voice(level);
    if (&active != &companion_.rig) companion_.rig.voice(0);
    else if (companion_.guest.look) companion_.guest.rig.voice(0);
    return true;
}
