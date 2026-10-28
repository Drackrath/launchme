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
// Settings → Dźwięk → "Folder dźwięków" (AppSettings::sound_folder); thread-safe.
void setUserSoundFolder(const std::filesystem::path& folder);
std::filesystem::path userSoundFolder();
// The playable audio files directly in `folder` (.mp3, .flac, .wav, .m4a, .wma), by name.
std::vector<std::wstring> soundFolderFiles(const std::filesystem::path& folder);
// A built-in sound: assets/ beside the executable only.
std::filesystem::path localSoundFile(const wchar_t* name);
// `name` as the folder lists it now (soundFolderFiles), else empty.
std::filesystem::path soundFolderFile(const std::filesystem::path& folder, const std::wstring& name);
