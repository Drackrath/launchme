#pragma once
// Linux only: Media Foundation's start and end. Audio and video decode through FFmpeg
// (platform/flac_ffmpeg.cpp, video_player_ffmpeg.cpp), which needs neither.
#include "Windows.h"

#define MF_VERSION 0x00020070
#define MFSTARTUP_LITE 0x1
#define MFSTARTUP_FULL 0
inline HRESULT MFStartup(ULONG, DWORD = MFSTARTUP_FULL) { return S_OK; }
inline HRESULT MFShutdown() { return S_OK; }
