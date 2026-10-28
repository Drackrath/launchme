#pragma once
// Linux only: COM's byte streams (objidl.h, objidlbase.h and wtypes.h on Windows) with the Windows
// SDK's names, values and signatures, implemented in src/linux/gfx/com_stream.cpp. Only what the
// launcher calls is declared; the vtables are this port's own, not Windows'.
#include "win32_base.hpp"

using OLECHAR = WCHAR;
using LPOLESTR = OLECHAR*;
using LPCOLESTR = const OLECHAR*;

// Status codes (winerror.h on Windows).
#ifndef STG_E_INVALIDFUNCTION
#define STG_E_INVALIDFUNCTION ((HRESULT)0x80030001)
#define STG_E_FILENOTFOUND ((HRESULT)0x80030002)
#define STG_E_ACCESSDENIED ((HRESULT)0x80030005)
#define STG_E_INVALIDPOINTER ((HRESULT)0x80030009)
#define STG_E_WRITEFAULT ((HRESULT)0x8003001D)
#define STG_E_READFAULT ((HRESULT)0x8003001E)
#define STG_E_INVALIDPARAMETER ((HRESULT)0x80030057)
#define STG_E_MEDIUMFULL ((HRESULT)0x80030070)
#define STG_E_INVALIDFLAG ((HRESULT)0x800300FF)
#endif

enum STGTY : DWORD { STGTY_STORAGE = 1, STGTY_STREAM = 2, STGTY_LOCKBYTES = 3, STGTY_PROPERTY = 4 };
enum STREAM_SEEK : DWORD { STREAM_SEEK_SET = 0, STREAM_SEEK_CUR = 1, STREAM_SEEK_END = 2 };
enum STATFLAG : DWORD { STATFLAG_DEFAULT = 0, STATFLAG_NONAME = 1, STATFLAG_NOOPEN = 2 };
enum STGC : DWORD { STGC_DEFAULT = 0, STGC_OVERWRITE = 1, STGC_ONLYIFCURRENT = 2, STGC_DANGEROUSLYCOMMITMERELYTODISKCACHE = 4, STGC_CONSOLIDATE = 8 };

// pwcsName is CoTaskMemAlloc'd (free it with CoTaskMemFree) unless STATFLAG_NONAME is asked for.
struct STATSTG {
    LPOLESTR pwcsName; DWORD type; ULARGE_INTEGER cbSize; FILETIME mtime; FILETIME ctime; FILETIME atime;
    DWORD grfMode; DWORD grfLocksSupported; CLSID clsid; DWORD grfStateBits; DWORD reserved;
};

struct ISequentialStream : public IUnknown {
    LM_IID(0x0c733a30, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d);
    using Base = IUnknown;
    // S_FALSE when fewer than `size` bytes were left.
    virtual HRESULT STDMETHODCALLTYPE Read(void* data, ULONG size, ULONG* read) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(const void* data, ULONG size, ULONG* written) = 0;
};

struct IStream : public ISequentialStream {
    LM_IID(0x0000000c, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
    using Base = ISequentialStream;
    virtual HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* position) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit(DWORD commitFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stat(STATSTG* statistics, DWORD statFlags) = 0;
};
using LPSTREAM = IStream*;
