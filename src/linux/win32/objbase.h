#pragma once
// Linux only: objbase.h's part the launcher uses, the COM runtime (combaseapi.h) plus its
// initialisation flags and the storage modes (coml2api.h on Windows) of SHCreateStreamOnFileEx.
#include "combaseapi.h"

enum COINIT : DWORD {
    COINIT_MULTITHREADED = 0x0, COINIT_APARTMENTTHREADED = 0x2, COINIT_DISABLE_OLE1DDE = 0x4, COINIT_SPEED_OVER_MEMORY = 0x8,
};

#define STGM_READ 0x00000000L
#define STGM_WRITE 0x00000001L
#define STGM_READWRITE 0x00000002L
#define STGM_SHARE_DENY_NONE 0x00000040L
#define STGM_SHARE_DENY_READ 0x00000030L
#define STGM_SHARE_DENY_WRITE 0x00000020L
#define STGM_SHARE_EXCLUSIVE 0x00000010L
#define STGM_CREATE 0x00001000L
#define STGM_FAILIFTHERE 0x00000000L
