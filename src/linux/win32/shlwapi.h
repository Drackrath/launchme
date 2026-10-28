#pragma once
// Linux only: shlwapi.h's streams with the Windows SDK's names and signatures, implemented in
// src/linux/gfx/com_stream.cpp.
#include "objbase.h"

// A growable memory stream starting as a copy of `initial` (may be null); null on failure.
IStream* SHCreateMemStream(const BYTE* initial, UINT size);
// A file stream. grfMode: STGM_READ, STGM_WRITE or STGM_READWRITE, with STGM_CREATE to create or
// truncate the file; without it `create` makes a new file (failing when it exists) or opens an
// existing one. Share modes and attributes have no meaning here; `streamTemplate` must be null.
HRESULT SHCreateStreamOnFileEx(LPCWSTR file, DWORD grfMode, DWORD attributes, BOOL create, IStream* streamTemplate, IStream** stream);
