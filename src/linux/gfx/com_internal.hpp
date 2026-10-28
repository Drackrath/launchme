#pragma once
// Linux only: the COM port's internals (com_*.cpp) that the WIC port (wic_*.cpp) shares: the one
// stream class behind IStream and IWICStream, and object creation without exceptions at the COM
// boundary.
#include "../win32/wincodec.h"

#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lm_com {
// A new object for an out parameter: E_INVALIDARG for a null one, E_OUTOFMEMORY when it cannot be made.
template <class T, class Interface, class... Arguments>
HRESULT make(Interface** out, Arguments&&... arguments) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    try { *out = new T(std::forward<Arguments>(arguments)...); return S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
// A method body that allocates: running out of memory is E_OUTOFMEMORY, not an exception.
template <class Body>
HRESULT guarded(Body&& body) {
    try { return body(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}

// Memory or a file behind IStream: nothing until IWICStream::Initialize* or one of the creating
// functions in com_stream.cpp gives it a backing. One thread at a time.
class Stream final : public Object<IWICStream> {
public:
    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG size, ULONG* read) override;
    HRESULT STDMETHODCALLTYPE Write(const void* data, ULONG size, ULONG* written) override;
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) override;
    HRESULT STDMETHODCALLTYPE Commit(DWORD commitFlags) override;
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* statistics, DWORD statFlags) override;
    HRESULT STDMETHODCALLTYPE InitializeFromFilename(LPCWSTR fileName, DWORD desiredAccess) override;
    HRESULT STDMETHODCALLTYPE InitializeFromFilename(const char* fileName, DWORD desiredAccess) override;
    HRESULT STDMETHODCALLTYPE InitializeFromMemory(WICInProcPointer buffer, DWORD bufferSize) override;

    // Growable memory of its own, starting as `bytes`.
    void own(std::vector<uint8_t> bytes);
    // A file opened with POSIX open() flags; `mode` is the STGM access Stat reports.
    HRESULT open(const std::string& file, int flags, DWORD mode);
    // The caller's memory from InitializeFromMemory; empty for every other backing.
    std::span<const uint8_t> borrowed() const;

private:
    ~Stream() override;
    enum class Backing { none, borrowed, owned, file };
    Backing backing_ = Backing::none;
    std::span<BYTE> borrowed_;
    std::vector<uint8_t> owned_;
    uint64_t position_ = 0; // Of the memory backings; a file keeps its own offset.
    int file_ = -1;
    std::wstring name_;
    DWORD mode_ = 0;
};
} // namespace lm_com
