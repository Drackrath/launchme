#pragma once
// Linux only: DirectWrite 1.1 (as the Windows SDK's dwrite_1.h): character spacing on layouts.
#include "dwrite.h"

struct IDWriteFactory1 : public IDWriteFactory {
    LM_IID(0x30572f99, 0xdac6, 0x41db, 0xa1, 0x6e, 0x04, 0x86, 0x30, 0x7e, 0x60, 0x6a);
    using Base = IDWriteFactory;
};

struct IDWriteTextLayout1 : public IDWriteTextLayout {
    LM_IID(0x9064d822, 0x80a7, 0x465c, 0xa9, 0x86, 0xdf, 0x65, 0xf7, 0x8b, 0x8f, 0xeb);
    using Base = IDWriteTextLayout;
    virtual HRESULT STDMETHODCALLTYPE SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth,
                                                          DWRITE_TEXT_RANGE textRange) = 0;
};
