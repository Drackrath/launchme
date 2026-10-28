#pragma once
// Linux only: the message-cracker macros the launcher uses.
#include "Windows.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
