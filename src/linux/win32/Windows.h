#pragma once
// Linux only: the Windows surface of the launcher's UI layer (see win32_base.hpp). The lowercase
// spelling lives in lower/windows.h (one folder cannot hold both names on a Windows checkout).
#include "win32_base.hpp"
#include "win32_kernel.hpp"
#include "win32_user.hpp"
#include "objbase.h"

#include <cwctype>

// No min/max macros: the launcher builds with NOMINMAX on Windows.
