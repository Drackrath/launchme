#include "flac.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace {
std::filesystem::path knownDesktop() {
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) return {};
    std::filesystem::path directory(desktop);
    CoTaskMemFree(desktop);
    return directory;
}
}

std::filesystem::path localSoundtrackDirectory() {
    const auto desktop = knownDesktop();
    return desktop.empty() ? std::filesystem::path{} : desktop / L"nintendo-3ds-original-soundtrack";
}

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
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return files;
}

std::filesystem::path localSoundFile(const wchar_t* name) {
    if (!name || !*name) return {};
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (length && length < std::size(executable)) {
        const auto bundled = std::filesystem::path(executable).parent_path() / L"assets" / name;
        if (std::filesystem::exists(bundled)) return bundled;
    }
    if (const auto folder = userSoundFolder(); !folder.empty()) {
        std::error_code error;
        const auto chosen = folder / name;
        if (std::filesystem::exists(chosen, error)) return chosen;
        // Also one level down: a soundtrack folder kept inside the chosen one.
        int checked = 0;
        for (std::filesystem::directory_iterator item(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
             !error && item != end && checked < 64; item.increment(error)) {
            if (!item->is_directory(error)) continue;
            ++checked;
            const auto nested = item->path() / name;
            if (std::filesystem::exists(nested, error)) return nested;
        }
    }
    const auto soundtrack = localSoundtrackDirectory();
    if (!soundtrack.empty()) {
        const auto bundled = soundtrack / name;
        if (std::filesystem::exists(bundled)) return bundled;
    }
    const auto desktop = knownDesktop();
    if (!desktop.empty()) {
        const auto loose = desktop / name;
        if (std::filesystem::exists(loose)) return loose;
    }
    return {};
}

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
