// Silent PCM fixtures exercise real XAudio2 buffer completion, not a guessed boot duration.
int LauncherUiTest::startupAudio(HWND window) {
    Launcher app; app.hwnd_ = window;
    const auto clip = [] {
        AudioClip value;
        const WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16, 0};
        value.format.resize(sizeof(format)); memcpy(value.format.data(), &format, sizeof(format));
        value.samples.resize(24000); // 250 ms of silence.
        return value;
    };
    auto& audio = app.audio_;
    audio.prepareBoot();
    audio.setMusic(clip());
    audio.resumeMusic(); audio.setMusicLevel(0.15f); audio.update(1);
    if (!audio.musicBlocked() || audio.music_running_ || audio.music_volume_ != 0) return 1870;
    const auto boot = [&] {
        auto message = std::make_unique<AudioMessage>();
        message->cue = SoundCue::boot;
        message->generation = app.sound_generation_[soundCueSlot(SoundCue::boot)];
        message->clip = clip(); app.onAudio(std::move(message));
    };
    boot();
    if (!app.boot_played_ || audio.boot_pending_) return 1871;
    if (audio.engine_) {
        if (!audio.musicBlocked() || audio.music_running_) return 1872;
        audio.update(100); // Even a large frame delta cannot end a voice that is still playing.
        if (audio.music_running_) return 1873;
        audio.stopMusic(); // Launching a game during the boot must preserve its pause.
        const auto deadline = GetTickCount64() + 2000;
        while (audio.musicBlocked() && GetTickCount64() < deadline) { Sleep(10); audio.update(0.01f); }
        if (audio.musicBlocked() || audio.music_running_ || audio.music_target_ != 0) return 1874;
        audio.resumeMusic(); audio.update(0.01f);
        if (!audio.music_running_) return 1875;
    }
    audio.clearMusic();
    if (audio.music_voice_ || audio.music_running_ || !audio.music_clip_.samples.empty()) return 1876;
    // Disabled boot releases startup immediately, without loading any sound file.
    audio.prepareBoot(); app.requestSound(SoundCue::boot, 1, false);
    if (audio.musicBlocked() || !app.boot_played_) return 1877;
    audio.setMusic(clip()); audio.update(0.01f);
    if (audio.engine_ && !audio.music_running_) return 1878;
    audio.clearMusic(); audio.prepareBoot(); app.boot_played_ = false;
    auto missing = std::make_unique<AudioMessage>(); missing->cue = SoundCue::boot;
    missing->generation = app.sound_generation_[soundCueSlot(SoundCue::boot)];
    app.onAudio(std::move(missing));
    if (audio.musicBlocked() || !app.boot_played_) return 1879;
    return 0;
}
