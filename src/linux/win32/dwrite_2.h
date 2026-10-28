#pragma once
// Linux only: DirectWrite 2 (as the Windows SDK's dwrite_2.h): the factory step to dwrite_3.h.
#include "dwrite_1.h"

struct IDWriteFactory2 : public IDWriteFactory1 {
    LM_IID(0x0439fc60, 0xca44, 0x4994, 0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec);
    using Base = IDWriteFactory1;
};
