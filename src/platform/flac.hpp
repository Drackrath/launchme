#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

struct AudioClip {
    std::vector<uint8_t> format; // WAVEFORMATEX, including a possible extension.
    std::vector<uint8_t> samples;
};

// Called on a COM-initialized background thread after MFStartup (Windows; FFmpeg elsewhere,
// flac_ffmpeg.cpp). Any audio file Media Foundation reads, despite the name.
AudioClip decodeFlac(const std::filesystem::path& path, size_t max_bytes, const std::function<bool()>& cancelled = {});
std::filesystem::path localSoundtrackDirectory();
// Settings → Dźwięk → "Folder dźwięków" (AppSettings::sound_folder); thread-safe, read by the
// audio worker. Searched after the bundled assets and before the old desktop locations.
void setUserSoundFolder(const std::filesystem::path& folder);
std::filesystem::path userSoundFolder();
// The playable audio files directly in `folder` (.mp3, .flac, .wav, .m4a, .wma), by name.
std::vector<std::wstring> soundFolderFiles(const std::filesystem::path& folder);
std::filesystem::path localSoundFile(const wchar_t* name);
