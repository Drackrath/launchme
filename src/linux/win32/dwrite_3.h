#pragma once
// Linux only: DirectWrite 3 (as the Windows SDK's dwrite_3.h): font collections built from font
// files. The files are registered with fontconfig as application fonts.
#include "dwrite_2.h"

struct IDWriteFontFaceReference : public IUnknown {
    LM_IID(0x5e7fa7ca, 0xdde3, 0x424c, 0x89, 0xf0, 0x9f, 0xcd, 0x6f, 0xed, 0x58, 0xcd);
    using Base = IUnknown;
};

struct IDWriteFontSet : public IUnknown {
    LM_IID(0x53585141, 0xd9f8, 0x4095, 0x83, 0x21, 0xd7, 0x3c, 0xf6, 0xbd, 0x11, 0x6b);
    using Base = IUnknown;
};

struct IDWriteFontSetBuilder : public IUnknown {
    LM_IID(0x2f642afe, 0x9c68, 0x4f40, 0xb8, 0xbe, 0x45, 0x74, 0x01, 0xaf, 0xcb, 0x3d);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE AddFontFaceReference(IDWriteFontFaceReference* fontFaceReference) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFontSet(IDWriteFontSet** fontSet) = 0;
};

struct IDWriteFontCollection1 : public IDWriteFontCollection {
    LM_IID(0x53585141, 0xd9f8, 0x4095, 0x83, 0x21, 0xd7, 0x3c, 0xf6, 0xbd, 0x11, 0x6c);
    using Base = IDWriteFontCollection;
};

struct IDWriteFactory3 : public IDWriteFactory2 {
    LM_IID(0x9a1b41c3, 0xd3bb, 0x466a, 0x87, 0xfc, 0xfe, 0x67, 0x55, 0x6a, 0x3b, 0x65);
    using Base = IDWriteFactory2;
    virtual HRESULT STDMETHODCALLTYPE CreateFontFaceReference(const WCHAR* filePath, const FILETIME* lastWriteTime, UINT32 faceIndex,
                                                              DWRITE_FONT_SIMULATIONS fontSimulations,
                                                              IDWriteFontFaceReference** fontFaceReference) = 0;
    // Linux: std::filesystem::path::c_str() is a UTF-8 char string here, not a WCHAR one.
    virtual HRESULT STDMETHODCALLTYPE CreateFontFaceReference(const char* filePath, const FILETIME* lastWriteTime, UINT32 faceIndex,
                                                              DWRITE_FONT_SIMULATIONS fontSimulations,
                                                              IDWriteFontFaceReference** fontFaceReference) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFontSetBuilder(IDWriteFontSetBuilder** fontSetBuilder) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFontCollectionFromFontSet(IDWriteFontSet* fontSet, IDWriteFontCollection1** fontCollection) = 0;
};
