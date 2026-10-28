#include "flac.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#else
#include "system.hpp"
#include "unicode_text.hpp"
#include <fstream>
#endif

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <mutex>

#ifdef _WIN32
using Microsoft::WRL::ComPtr;
#endif

namespace {
std::mutex& soundFolderMutex() { static std::mutex mutex; return mutex; }
std::filesystem::path& soundFolderPath() { static std::filesystem::path path; return path; }
}

void setUserSoundFolder(const std::filesystem::path& folder) {
    std::lock_guard lock(soundFolderMutex());
    soundFolderPath() = folder;
}

std::filesystem::path userSoundFolder() {
    std::lock_guard lock(soundFolderMutex());
    return soundFolderPath();
}

std::vector<std::wstring> soundFolderFiles(const std::filesystem::path& folder) {
    std::vector<std::wstring> files;
    std::error_code error;
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) return files;
    for (std::filesystem::directory_iterator item(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && item != end; item.increment(error)) {
        if (!item->is_regular_file(error)) continue;
        auto extension = item->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension == L".mp3" || extension == L".flac" || extension == L".wav" || extension == L".m4a" || extension == L".wma")
            files.push_back(item->path().filename().wstring());
        if (files.size() >= 500) break; // A music library pointed at by mistake stays a usable list.
    }
    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
#ifdef _WIN32
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
#else
        return unicode_text::compareIgnoreCase(a, b) < 0;
#endif
    });
    return files;
}

std::filesystem::path localSoundFile(const wchar_t* name) {
    if (!name || !*name) return {};
#ifdef _WIN32
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return {};
    const auto bundled = std::filesystem::path(executable).parent_path() / L"assets" / name;
#else
    const auto executable = system_info::executablePath();
    if (executable.empty()) return {};
    const auto bundled = executable.parent_path() / L"assets" / name;
#endif
    std::error_code error;
    return std::filesystem::is_regular_file(bundled, error) ? bundled : std::filesystem::path{};
}

std::filesystem::path soundFolderFile(const std::filesystem::path& folder, const std::wstring& name) {
    if (folder.empty() || name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos) return {};
    const auto files = soundFolderFiles(folder);
    // Only a file the folder lists now; a name remembered from an emptied folder plays nothing.
    const auto found = std::find_if(files.begin(), files.end(), [&](const std::wstring& file) {
#ifdef _WIN32
        return CompareStringOrdinal(file.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
        return unicode_text::equalIgnoreCase(file, name);
#endif
    });
    return found == files.end() ? std::filesystem::path{} : folder / *found;
}

#ifdef _WIN32 // Elsewhere: flac_ffmpeg.cpp.
AudioClip decodeFlac(const std::filesystem::path& path, size_t max_bytes, const std::function<bool()>& cancelled) {
    AudioClip clip;
    if (path.empty() || !std::filesystem::exists(path)) return clip;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> wanted, current;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, reader.GetAddressOf())) ||
        FAILED(MFCreateMediaType(wanted.GetAddressOf())) ||
        FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, wanted.Get())) ||
        FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), current.GetAddressOf()))) return {};
    WAVEFORMATEX* wave = nullptr;
    UINT32 size = 0;
    if (FAILED(MFCreateWaveFormatExFromMFMediaType(current.Get(), &wave, &size)) || !wave) return {};
    if (size >= sizeof(WAVEFORMATEX) && wave->wBitsPerSample == 16 && wave->nBlockAlign) {
        clip.format.resize(size);
        std::memcpy(clip.format.data(), wave, size);
    }
    CoTaskMemFree(wave);
    if (clip.format.empty()) return {};
    for (;;) {
        if (cancelled && cancelled()) return {};
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr,
                                      &flags, nullptr, sample.GetAddressOf())) ||
            (flags & MF_SOURCE_READERF_ERROR)) return {};
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) return {};
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length))) return {};
            if (length > max_bytes - std::min(max_bytes, clip.samples.size())) {
                buffer->Unlock();
                return {};
            }
            clip.samples.insert(clip.samples.end(), data, data + length);
            buffer->Unlock();
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    return clip.samples.empty() ? AudioClip{} : std::move(clip);
}
#endif
