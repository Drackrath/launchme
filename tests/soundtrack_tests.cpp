#include "../src/platform/flac.hpp"
#include "../src/platform/music_themes.hpp"

#include "../src/platform/wave_format.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <mfapi.h>
#endif

#include <filesystem>
#include <iostream>

#ifdef _WIN32
static void shutdownDecoder() { shutdownDecoder(); }
#else
static void shutdownDecoder() {} // FFmpeg needs no start or end.
#endif

int main() {
#ifdef _WIN32
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { CoUninitialize(); return 1; }
#endif
    {
        const auto file = localSoundFile(L"HorizonsTheme.mp3");
        const auto theme = decodeFlac(file, music_pcm_limit);
        if (file.parent_path().filename() != L"assets" || theme.format.size() < sizeof(WAVEFORMATEX) || theme.samples.empty()) {
            std::cerr << "Bundled Horizons theme missing or failed to decode\n"; shutdownDecoder(); return 5;
        }
        const auto* wave = reinterpret_cast<const WAVEFORMATEX*>(theme.format.data());
        if (!wave->nAvgBytesPerSec || !decodeFlac(file, music_pcm_limit, [] { return true; }).samples.empty()) {
            shutdownDecoder(); return 6;
        }
        std::cout << "Horizons theme: " << double(theme.samples.size()) / wave->nAvgBytesPerSec << " seconds, " << theme.samples.size() << " PCM bytes\n";
    }
    const auto directory = localSoundtrackDirectory();
    const auto music = directory / L"2-04. (Bonus) Home Menu - No Intro.flac";
    const auto cue = directory / L"2-05. (Bonus) Nintendo Network ID Settings - Loading (Chord).flac";
    if (!std::filesystem::exists(music) || !std::filesystem::exists(cue)) {
        std::cout << "Local soundtrack not installed; synthetic fallback available\n";
        shutdownDecoder();
        return 0;
    }
    for (const auto& theme : music_themes) {
        if (!theme.file) continue;
        const auto path = localSoundFile(theme.file);
        if (!std::filesystem::exists(path)) continue;
        const auto clip = decodeFlac(path, music_pcm_limit);
        if (clip.samples.empty()) { std::cerr << "Theme decode failed\n"; shutdownDecoder(); return 2; }
    }
    if (!decodeFlac(music, music_pcm_limit, [] { return true; }).samples.empty()) return 3;
    const auto bgm = decodeFlac(music, music_pcm_limit);
    const auto effect = decodeFlac(cue, 8 * 1024 * 1024);
    const auto boot = localSoundFile(L"psp_boot_sound.mp3");
    AudioClip boot_clip;
    if (!boot.empty()) {
        boot_clip = decodeFlac(boot, 16 * 1024 * 1024);
        if (boot_clip.samples.empty()) {
            std::cerr << "Boot sound decode failed\n";
            shutdownDecoder();
            return 4;
        }
    }
    shutdownDecoder();
    if (bgm.format.size() < sizeof(WAVEFORMATEX) || bgm.samples.empty() ||
        effect.format.size() < sizeof(WAVEFORMATEX) || effect.samples.empty()) {
        std::cerr << "Native FLAC decode failed\n";
        return 1;
    }
    std::cout << "Decoded BGM=" << bgm.samples.size() << " bytes; cue="
              << effect.samples.size() << " bytes";
    if (!boot_clip.samples.empty()) std::cout << "; boot=" << boot_clip.samples.size() << " bytes";
    std::cout << '\n';
}
