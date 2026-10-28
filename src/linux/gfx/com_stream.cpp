// COM's streams (objidl.h, combaseapi.h, shlwapi.h, IWICStream): the caller's memory in place,
// growable memory of the stream's own, or a file through a POSIX descriptor.
#include "com_internal.hpp"
#include "../win32/shlwapi.h"
#include "../../platform/text_encoding.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lm_com {
namespace {
// FILETIME: 100 ns ticks since 1601.
FILETIME fileTime(const timespec& time) {
    const uint64_t ticks = (static_cast<uint64_t>(time.tv_sec) + 11644473600ull) * 10000000ull + static_cast<uint64_t>(time.tv_nsec) / 100;
    return {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
}

// Win32 file errors (HRESULT_FROM_WIN32) for an errno, as CreateFile reports them.
HRESULT fileError(int error) {
    switch (error) {
    case ENOENT: return HRESULT_FROM_WIN32(2);                   // ERROR_FILE_NOT_FOUND
    case ENOTDIR: return HRESULT_FROM_WIN32(3);                  // ERROR_PATH_NOT_FOUND
    case EACCES: case EPERM: case EROFS: case EISDIR: return E_ACCESSDENIED;
    case EEXIST: return HRESULT_FROM_WIN32(80);                  // ERROR_FILE_EXISTS
    case ENOSPC: return HRESULT_FROM_WIN32(112);                 // ERROR_DISK_FULL
    case ENAMETOOLONG: return HRESULT_FROM_WIN32(206);           // ERROR_FILENAME_EXCED_RANGE
    case ENOMEM: return E_OUTOFMEMORY;
    default: return E_FAIL;
    }
}
} // namespace

Stream::~Stream() {
    if (file_ >= 0) ::close(file_);
}

void Stream::own(std::vector<uint8_t> bytes) {
    owned_ = std::move(bytes);
    backing_ = Backing::owned;
    mode_ = STGM_READWRITE;
}

HRESULT Stream::open(const std::string& file, int flags, DWORD mode) {
    if (backing_ != Backing::none) return WINCODEC_ERR_WRONGSTATE;
    const int descriptor = ::open(file.c_str(), flags | O_CLOEXEC, 0666);
    if (descriptor < 0) return fileError(errno);
    struct stat status{};
    if (::fstat(descriptor, &status) != 0 || S_ISDIR(status.st_mode)) {
        ::close(descriptor);
        return E_ACCESSDENIED; // CreateFile refuses a folder the same way.
    }
    file_ = descriptor;
    name_ = text_encoding::wideLenient(file);
    mode_ = mode;
    backing_ = Backing::file;
    return S_OK;
}

std::span<const uint8_t> Stream::borrowed() const {
    return backing_ == Backing::borrowed ? std::span<const uint8_t>(borrowed_) : std::span<const uint8_t>{};
}

HRESULT STDMETHODCALLTYPE Stream::Read(void* data, ULONG size, ULONG* read) {
    if (read) *read = 0;
    if (backing_ == Backing::none) return WINCODEC_ERR_NOTINITIALIZED;
    if (!data && size) return STG_E_INVALIDPOINTER;
    ULONG done = 0;
    if (backing_ == Backing::file) {
        while (done < size) {
            const ssize_t got = ::read(file_, static_cast<uint8_t*>(data) + done, size - done);
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) return STG_E_READFAULT;
            if (got == 0) break;
            done += static_cast<ULONG>(got);
        }
    } else {
        const size_t length = backing_ == Backing::owned ? owned_.size() : borrowed_.size();
        const uint8_t* bytes = backing_ == Backing::owned ? owned_.data() : borrowed_.data();
        if (position_ < length) {
            done = static_cast<ULONG>(std::min<uint64_t>(size, length - position_));
            std::memcpy(data, bytes + position_, done);
            position_ += done;
        }
    }
    if (read) *read = done;
    return done == size ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE Stream::Write(const void* data, ULONG size, ULONG* written) {
    if (written) *written = 0;
    if (backing_ == Backing::none) return WINCODEC_ERR_NOTINITIALIZED;
    if (!data && size) return STG_E_INVALIDPOINTER;
    ULONG done = 0;
    HRESULT result = S_OK;
    if (backing_ == Backing::file) {
        while (done < size) {
            const ssize_t put = ::write(file_, static_cast<const uint8_t*>(data) + done, size - done);
            if (put < 0 && errno == EINTR) continue;
            if (put < 0) { result = errno == ENOSPC ? STG_E_MEDIUMFULL : errno == EBADF ? STG_E_ACCESSDENIED : STG_E_WRITEFAULT; break; }
            done += static_cast<ULONG>(put);
        }
    } else if (backing_ == Backing::borrowed) {
        // The caller's buffer does not grow.
        done = position_ < borrowed_.size() ? static_cast<ULONG>(std::min<uint64_t>(size, borrowed_.size() - position_)) : 0;
        if (done) std::memcpy(borrowed_.data() + position_, data, done);
        if (done < size) result = STG_E_MEDIUMFULL;
        position_ += done;
    } else {
        result = guarded([&] {
            if (position_ + size > owned_.size()) owned_.resize(static_cast<size_t>(position_ + size));
            if (size) std::memcpy(owned_.data() + position_, data, size);
            done = size;
            return S_OK;
        });
        if (result == E_OUTOFMEMORY) result = STG_E_MEDIUMFULL;
        position_ += done;
    }
    if (written) *written = done;
    return result;
}

HRESULT STDMETHODCALLTYPE Stream::Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) {
    if (backing_ == Backing::none) return WINCODEC_ERR_NOTINITIALIZED;
    if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
    int64_t target;
    if (backing_ == Backing::file) {
        const int whence = origin == STREAM_SEEK_SET ? SEEK_SET : origin == STREAM_SEEK_CUR ? SEEK_CUR : SEEK_END;
        target = ::lseek(file_, move.QuadPart, whence);
        if (target < 0) return STG_E_INVALIDFUNCTION;
    } else {
        const int64_t length = static_cast<int64_t>(backing_ == Backing::owned ? owned_.size() : borrowed_.size());
        const int64_t base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? static_cast<int64_t>(position_) : length;
        target = base + move.QuadPart;
        if (target < 0) return STG_E_INVALIDFUNCTION;
        position_ = static_cast<uint64_t>(target);
    }
    if (position) position->QuadPart = static_cast<ULONGLONG>(target);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Stream::Commit(DWORD) {
    // Writes reach the memory or the file at once: nothing is buffered here.
    return backing_ == Backing::none ? WINCODEC_ERR_NOTINITIALIZED : S_OK;
}

HRESULT STDMETHODCALLTYPE Stream::Stat(STATSTG* statistics, DWORD statFlags) {
    if (!statistics) return STG_E_INVALIDPOINTER;
    if (backing_ == Backing::none) return WINCODEC_ERR_NOTINITIALIZED;
    *statistics = {};
    statistics->type = STGTY_STREAM;
    statistics->grfMode = mode_;
    if (backing_ == Backing::file) {
        struct stat status{};
        if (::fstat(file_, &status) != 0) return STG_E_ACCESSDENIED;
        statistics->cbSize.QuadPart = static_cast<ULONGLONG>(status.st_size);
        statistics->mtime = fileTime(status.st_mtim);
        statistics->ctime = fileTime(status.st_ctim);
        statistics->atime = fileTime(status.st_atim);
        if (!(statFlags & STATFLAG_NONAME)) {
            auto* name = static_cast<LPOLESTR>(CoTaskMemAlloc((name_.size() + 1) * sizeof(OLECHAR)));
            if (!name) return E_OUTOFMEMORY;
            std::copy(name_.c_str(), name_.c_str() + name_.size() + 1, name);
            statistics->pwcsName = name;
        }
    } else {
        statistics->cbSize.QuadPart = backing_ == Backing::owned ? owned_.size() : borrowed_.size();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Stream::InitializeFromFilename(LPCWSTR fileName, DWORD desiredAccess) {
    if (!fileName || !*fileName) return E_INVALIDARG;
    const auto name = text_encoding::utf8(fileName);
    return name.empty() ? E_INVALIDARG : InitializeFromFilename(name.c_str(), desiredAccess);
}

HRESULT STDMETHODCALLTYPE Stream::InitializeFromFilename(const char* fileName, DWORD desiredAccess) {
    if (!fileName || !*fileName) return E_INVALIDARG;
    const bool read = desiredAccess & GENERIC_READ, write = desiredAccess & GENERIC_WRITE;
    if (!read && !write) return E_INVALIDARG;
    // As WIC: reading opens an existing file, writing creates or empties it.
    if (!write) return open(fileName, O_RDONLY, STGM_READ);
    return open(fileName, (read ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC, read ? STGM_READWRITE : STGM_WRITE);
}

HRESULT STDMETHODCALLTYPE Stream::InitializeFromMemory(WICInProcPointer buffer, DWORD bufferSize) {
    if (!buffer) return E_INVALIDARG;
    if (backing_ != Backing::none) return WINCODEC_ERR_WRONGSTATE;
    borrowed_ = {buffer, bufferSize};
    backing_ = Backing::borrowed;
    mode_ = STGM_READWRITE;
    return S_OK;
}
} // namespace lm_com

HRESULT CreateStreamOnHGlobal(HGLOBAL memory, BOOL, LPSTREAM* stream) {
    if (!stream) return E_INVALIDARG;
    *stream = nullptr;
    if (memory) return E_INVALIDARG;
    lm_com::Stream* created = nullptr;
    const HRESULT result = lm_com::make<lm_com::Stream>(&created);
    if (FAILED(result)) return result;
    created->own({});
    *stream = created;
    return S_OK;
}

IStream* SHCreateMemStream(const BYTE* initial, UINT size) {
    lm_com::Stream* created = nullptr;
    if (FAILED(lm_com::make<lm_com::Stream>(&created))) return nullptr;
    const HRESULT result = lm_com::guarded([&] {
        created->own(initial ? std::vector<uint8_t>(initial, initial + size) : std::vector<uint8_t>(size));
        return S_OK;
    });
    if (FAILED(result)) { created->Release(); return nullptr; }
    return created;
}

HRESULT SHCreateStreamOnFileEx(LPCWSTR file, DWORD grfMode, DWORD, BOOL create, IStream* streamTemplate, IStream** stream) {
    if (!stream) return E_INVALIDARG;
    *stream = nullptr;
    if (!file || !*file || streamTemplate) return E_INVALIDARG;
    const auto name = text_encoding::utf8(file);
    if (name.empty()) return E_INVALIDARG;
    const DWORD access = grfMode & 0x3;
    if (access > STGM_READWRITE) return STG_E_INVALIDFLAG;
    int flags = access == STGM_READ ? O_RDONLY : access == STGM_WRITE ? O_WRONLY : O_RDWR;
    if (grfMode & STGM_CREATE) flags |= O_CREAT | O_TRUNC;
    else if (create) flags |= O_CREAT | O_EXCL;
    lm_com::Stream* created = nullptr;
    HRESULT result = lm_com::make<lm_com::Stream>(&created);
    if (SUCCEEDED(result)) result = created->open(name, flags, access);
    if (FAILED(result)) { if (created) created->Release(); return result; }
    *stream = created;
    return S_OK;
}
