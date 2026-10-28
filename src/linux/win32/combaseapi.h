#pragma once
// Linux only: the COM runtime's entry points (combaseapi.h on Windows) with the Windows SDK's names,
// values and signatures, implemented in src/linux/gfx/com_runtime.cpp. There are no apartments:
// CoInitializeEx only counts per thread, answering S_OK and then S_FALSE as Windows does, and
// CoCreateInstance creates the classes registered in this process (WIC's imaging factory and
// whatever lm_com::registerClass adds).
#include "objidl.h"

#ifndef REGDB_E_CLASSNOTREG
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#endif
#ifndef CLASS_E_NOAGGREGATION
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)
#endif

enum CLSCTX : DWORD { CLSCTX_INPROC_SERVER = 0x1, CLSCTX_INPROC_HANDLER = 0x2, CLSCTX_LOCAL_SERVER = 0x4, CLSCTX_REMOTE_SERVER = 0x10 };
#define CLSCTX_INPROC (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER)
#define CLSCTX_ALL (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)

using LPUNKNOWN = IUnknown*;

HRESULT CoInitializeEx(LPVOID reserved, DWORD coInit);
void CoUninitialize();
// REGDB_E_CLASSNOTREG for a class nobody registered; aggregation is not supported.
HRESULT CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID riid, LPVOID* object);
LPVOID CoTaskMemAlloc(SIZE_T size);
void CoTaskMemFree(LPVOID memory);
// A growable memory stream of its own: `memory` must be null (an HGLOBAL belongs to kernel32's
// port), and the stream always frees its memory when released.
HRESULT CreateStreamOnHGlobal(HGLOBAL memory, BOOL deleteOnRelease, LPSTREAM* stream);

namespace lm_com {
// Creates an object answering `riid`, as QueryInterface does (*object null on failure).
using ClassFactory = HRESULT (*)(REFIID riid, void** object);
// CoCreateInstance(clsid, ...) calls `factory` from now on, replacing an earlier registration.
void registerClass(const CLSID& clsid, ClassFactory factory);
} // namespace lm_com
