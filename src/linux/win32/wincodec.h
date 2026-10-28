#pragma once
// Linux only: the Windows Imaging Component's interfaces with the Windows SDK's names, values and
// signatures, implemented on image_codec (libpng, libjpeg-turbo, libwebp) in src/linux/gfx/wic_*.cpp:
// PNG, JPEG, WebP and BMP decode, PNG and JPEG encode. Only what the launcher calls is declared;
// the vtables are this port's own, not Windows'.
#include "objbase.h"
#include "propidl.h"

// Access rights of CreateDecoderFromFilename and IWICStream::InitializeFromFilename (winnt.h).
#ifndef GENERIC_READ
#define GENERIC_READ (0x80000000L)
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE (0x40000000L)
#endif

// Status codes (wincodec.h and winerror.h on Windows).
#define WINCODEC_ERR_GENERIC_ERROR E_FAIL
#define WINCODEC_ERR_INVALIDPARAMETER E_INVALIDARG
#define WINCODEC_ERR_OUTOFMEMORY E_OUTOFMEMORY
#define WINCODEC_ERR_NOTIMPLEMENTED E_NOTIMPL
#ifndef WINCODEC_ERR_WRONGSTATE
#define WINCODEC_ERR_WRONGSTATE ((HRESULT)0x88982F04)
#define WINCODEC_ERR_VALUEOUTOFRANGE ((HRESULT)0x88982F05)
#define WINCODEC_ERR_UNKNOWNIMAGEFORMAT ((HRESULT)0x88982F07)
#define WINCODEC_ERR_NOTINITIALIZED ((HRESULT)0x88982F0C)
#define WINCODEC_ERR_ALREADYLOCKED ((HRESULT)0x88982F0D)
#define WINCODEC_ERR_PROPERTYNOTFOUND ((HRESULT)0x88982F40)
#define WINCODEC_ERR_PROPERTYNOTSUPPORTED ((HRESULT)0x88982F41)
#define WINCODEC_ERR_CODECTOOMANYSCANLINES ((HRESULT)0x88982F46)
#define WINCODEC_ERR_INTERNALERROR ((HRESULT)0x88982F48)
#define WINCODEC_ERR_COMPONENTNOTFOUND ((HRESULT)0x88982F50)
#define WINCODEC_ERR_IMAGESIZEOUTOFRANGE ((HRESULT)0x88982F51)
#define WINCODEC_ERR_BADIMAGE ((HRESULT)0x88982F60)
#define WINCODEC_ERR_BADHEADER ((HRESULT)0x88982F61)
#define WINCODEC_ERR_FRAMEMISSING ((HRESULT)0x88982F62)
#define WINCODEC_ERR_STREAMWRITE ((HRESULT)0x88982F71)
#define WINCODEC_ERR_STREAMREAD ((HRESULT)0x88982F72)
#define WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT ((HRESULT)0x88982F80)
#define WINCODEC_ERR_UNSUPPORTEDOPERATION ((HRESULT)0x88982F81)
#define WINCODEC_ERR_INSUFFICIENTBUFFER ((HRESULT)0x88982F8C)
#define WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE ((HRESULT)0x88982F8E)
#endif

using WICPixelFormatGUID = GUID;
using REFWICPixelFormatGUID = const GUID&;
using WICInProcPointer = BYTE*;
struct WICRect { INT X; INT Y; INT Width; INT Height; };

enum WICDecodeOptions : UINT32 { WICDecodeMetadataCacheOnDemand = 0, WICDecodeMetadataCacheOnLoad = 0x1 };
enum WICBitmapCreateCacheOption : UINT32 { WICBitmapNoCache = 0, WICBitmapCacheOnDemand = 0x1, WICBitmapCacheOnLoad = 0x2 };
enum WICBitmapEncoderCacheOption : UINT32 { WICBitmapEncoderCacheInMemory = 0, WICBitmapEncoderCacheTempFile = 0x1, WICBitmapEncoderNoCache = 0x2 };
enum WICBitmapInterpolationMode : UINT32 {
    WICBitmapInterpolationModeNearestNeighbor = 0, WICBitmapInterpolationModeLinear = 0x1, WICBitmapInterpolationModeCubic = 0x2,
    WICBitmapInterpolationModeFant = 0x3, WICBitmapInterpolationModeHighQualityCubic = 0x4,
};
enum WICBitmapPaletteType : UINT32 {
    WICBitmapPaletteTypeCustom = 0, WICBitmapPaletteTypeMedianCut = 0x1, WICBitmapPaletteTypeFixedBW = 0x2,
    WICBitmapPaletteTypeFixedHalftone8 = 0x3, WICBitmapPaletteTypeFixedHalftone27 = 0x4, WICBitmapPaletteTypeFixedHalftone64 = 0x5,
    WICBitmapPaletteTypeFixedHalftone125 = 0x6, WICBitmapPaletteTypeFixedHalftone216 = 0x7,
    WICBitmapPaletteTypeFixedWebPalette = WICBitmapPaletteTypeFixedHalftone216, WICBitmapPaletteTypeFixedHalftone252 = 0x8,
    WICBitmapPaletteTypeFixedHalftone256 = 0x9, WICBitmapPaletteTypeFixedGray4 = 0xa, WICBitmapPaletteTypeFixedGray16 = 0xb,
    WICBitmapPaletteTypeFixedGray256 = 0xc,
};
enum WICBitmapDitherType : UINT32 {
    WICBitmapDitherTypeNone = 0, WICBitmapDitherTypeSolid = 0, WICBitmapDitherTypeOrdered4x4 = 0x1, WICBitmapDitherTypeOrdered8x8 = 0x2,
    WICBitmapDitherTypeOrdered16x16 = 0x3, WICBitmapDitherTypeSpiral4x4 = 0x4, WICBitmapDitherTypeSpiral8x8 = 0x5,
    WICBitmapDitherTypeDualSpiral4x4 = 0x6, WICBitmapDitherTypeDualSpiral8x8 = 0x7, WICBitmapDitherTypeErrorDiffusion = 0x8,
};
enum WICBitmapTransformOptions : UINT32 {
    WICBitmapTransformRotate0 = 0, WICBitmapTransformRotate90 = 0x1, WICBitmapTransformRotate180 = 0x2,
    WICBitmapTransformRotate270 = 0x3, WICBitmapTransformFlipHorizontal = 0x8, WICBitmapTransformFlipVertical = 0x10,
};
enum WICBitmapLockFlags : UINT32 { WICBitmapLockRead = 0x1, WICBitmapLockWrite = 0x2 };

// Containers: these decode (Png and Jpeg also encode); any other image is WINCODEC_ERR_COMPONENTNOTFOUND.
inline constexpr GUID GUID_ContainerFormatBmp{0x0af1d87e, 0xfcfe, 0x4188, {0xbd, 0xeb, 0xa7, 0x90, 0x64, 0x71, 0xcb, 0xe3}};
inline constexpr GUID GUID_ContainerFormatPng{0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};
inline constexpr GUID GUID_ContainerFormatJpeg{0x19e4a5aa, 0x5662, 0x4fc5, {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}};
inline constexpr GUID GUID_ContainerFormatWebp{0xe094b0e2, 0x67f2, 0x45b3, {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3}};

// Pixel formats: every one of these converts to every other one.
inline constexpr GUID GUID_WICPixelFormat8bppGray{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x08}};
inline constexpr GUID GUID_WICPixelFormat8bppAlpha{0xe6cd0116, 0xeeba, 0x4161, {0xaa, 0x85, 0x27, 0xdd, 0x9f, 0xb3, 0xa8, 0x95}};
inline constexpr GUID GUID_WICPixelFormat24bppBGR{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}};
inline constexpr GUID GUID_WICPixelFormat24bppRGB{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0d}};
inline constexpr GUID GUID_WICPixelFormat32bppBGR{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e}};
inline constexpr GUID GUID_WICPixelFormat32bppBGRA{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}};
inline constexpr GUID GUID_WICPixelFormat32bppPBGRA{0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};
inline constexpr GUID GUID_WICPixelFormat32bppRGBA{0xf5c7ad2d, 0x6a8d, 0x43dd, {0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9}};
inline constexpr GUID GUID_WICPixelFormat32bppPRGBA{0x3cc4a650, 0xa527, 0x4d37, {0xa9, 0x16, 0x31, 0x42, 0xc7, 0xeb, 0xed, 0xba}};

// CoCreateInstance classes. Both create the same factory.
inline constexpr CLSID CLSID_WICImagingFactory{0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
inline constexpr CLSID CLSID_WICImagingFactory1 = CLSID_WICImagingFactory;
inline constexpr CLSID CLSID_WICImagingFactory2{0x317d06e8, 0x5f24, 0x433d, {0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2}};

struct IWICPalette; // Palettes are not supported: pass null wherever one is asked for.

struct IWICBitmapSource : public IUnknown {
    LM_IID(0x00000120, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResolution(double* dpiX, double* dpiY) = 0;
    // rect null: the whole image. Rows are `stride` bytes apart within `bufferSize` bytes.
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT bufferSize, BYTE* buffer) = 0;
};

struct IWICBitmapLock : public IUnknown {
    LM_IID(0x00000123, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStride(UINT* stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* bufferSize, WICInProcPointer* data) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pixelFormat) = 0;
};

struct IWICBitmap : public IWICBitmapSource {
    LM_IID(0x00000121, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IWICBitmapSource;
    // Any number of read locks, or one write lock (WINCODEC_ERR_ALREADYLOCKED otherwise), until
    // the lock is released.
    virtual HRESULT STDMETHODCALLTYPE Lock(const WICRect* rect, DWORD flags, IWICBitmapLock** lock) = 0;
};

struct IWICFormatConverter : public IWICBitmapSource {
    LM_IID(0x00000301, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IWICBitmapSource;
    // Straight and premultiplied alpha convert as WIC does; dithering, palettes and the alpha
    // threshold (palette formats only) have no meaning for the formats here.
    virtual HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither,
                                                 IWICPalette* palette, double alphaThresholdPercent, WICBitmapPaletteType paletteTranslate) = 0;
};

struct IWICBitmapScaler : public IWICBitmapSource {
    LM_IID(0x00000302, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IWICBitmapSource;
    // The source is read and scaled once, at the first CopyPixels; the pixel format stays the source's.
    virtual HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, UINT width, UINT height, WICBitmapInterpolationMode mode) = 0;
};

struct IWICBitmapClipper : public IWICBitmapSource {
    LM_IID(0xe4fbcf03, 0x223d, 0x4e81, 0x93, 0x33, 0xd6, 0x35, 0x55, 0x6d, 0xd1, 0xb5);
    using Base = IWICBitmapSource;
    virtual HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* source, const WICRect* rect) = 0;
};

// A decoded frame reports the format its file stores (PNG with alpha: 32bppBGRA, without:
// 24bppBGR, grey: 8bppGray; JPEG: 24bppBGR or 8bppGray), and decodes at the first CopyPixels.
struct IWICBitmapFrameDecode : public IWICBitmapSource {
    LM_IID(0x3b16811b, 0x6a43, 0x4ec9, 0xa8, 0x13, 0x3d, 0x93, 0x0c, 0x13, 0xb9, 0x40);
    using Base = IWICBitmapSource;
};

// Every frame answers it; a JPEG decodes natively at 1/2, 1/4 and 1/8 of its size (DCT scaling),
// other images only at their own size.
struct IWICBitmapSourceTransform : public IUnknown {
    LM_IID(0x3b16811b, 0x6a43, 0x4ec9, 0xb7, 0x13, 0x3d, 0x5a, 0x0c, 0x13, 0xb9, 0x40);
    using Base = IUnknown;
    // width x height must be a size GetClosestSize returns; rect is in that size. dstFormat null:
    // the frame's format. Only WICBitmapTransformRotate0.
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT width, UINT height, WICPixelFormatGUID* dstFormat,
                                                 WICBitmapTransformOptions dstTransform, UINT stride, UINT bufferSize, BYTE* buffer) = 0;
    // The smallest native size at least as large as asked for (the full size when none is).
    virtual HRESULT STDMETHODCALLTYPE GetClosestSize(UINT* width, UINT* height) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestPixelFormat(WICPixelFormatGUID* dstFormat) = 0;
};

struct IWICBitmapDecoder : public IUnknown {
    LM_IID(0x9edde9e7, 0x8dee, 0x47ea, 0x99, 0xdf, 0xe6, 0xfa, 0xf2, 0xed, 0x44, 0xbf);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* containerFormat) = 0;
    // Always one: only the first frame of an image is decoded.
    virtual HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrame(UINT index, IWICBitmapFrameDecode** frame) = 0;
};

struct IWICComponentInfo : public IUnknown {
    LM_IID(0x23bc3f0a, 0x698b, 0x4357, 0x88, 0x6b, 0xf2, 0x4d, 0x50, 0x67, 0x13, 0x34);
    using Base = IUnknown;
};

struct IWICPixelFormatInfo : public IWICComponentInfo {
    LM_IID(0xe8eda601, 0x3d48, 0x431a, 0xab, 0x44, 0x69, 0x05, 0x9b, 0xe8, 0x8b, 0xbe);
    using Base = IWICComponentInfo;
    virtual HRESULT STDMETHODCALLTYPE GetBitsPerPixel(UINT* bitsPerPixel) = 0;
};

struct IWICStream : public IStream {
    LM_IID(0x135ff860, 0x22b7, 0x4ddf, 0xb0, 0xf6, 0x21, 0x8f, 0x4f, 0x29, 0x9a, 0x43);
    using Base = IStream;
    // GENERIC_READ opens an existing file; GENERIC_WRITE creates or truncates it.
    virtual HRESULT STDMETHODCALLTYPE InitializeFromFilename(LPCWSTR fileName, DWORD desiredAccess) = 0;
    // Linux addition: std::filesystem::path::c_str() is a UTF-8 string here.
    virtual HRESULT STDMETHODCALLTYPE InitializeFromFilename(const char* fileName, DWORD desiredAccess) = 0;
    // The caller's memory, read (and written) in place: it must outlive the stream and every
    // decoder reading it.
    virtual HRESULT STDMETHODCALLTYPE InitializeFromMemory(WICInProcPointer buffer, DWORD bufferSize) = 0;
};

// The image goes to the encoder's stream when the frame is committed.
struct IWICBitmapFrameEncode : public IUnknown {
    LM_IID(0x00000105, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IUnknown;
    // JPEG reads "ImageQuality" (VT_R4, 0-1, 0.9 by default) from the options CreateNewFrame gave.
    virtual HRESULT STDMETHODCALLTYPE Initialize(IPropertyBag2* encoderOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSize(UINT width, UINT height) = 0;
    // Replaced by the closest format the container stores: PNG 32bppBGRA, 24bppBGR or 8bppGray;
    // JPEG 24bppBGR or 8bppGray.
    virtual HRESULT STDMETHODCALLTYPE SetPixelFormat(WICPixelFormatGUID* pixelFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE WritePixels(UINT lineCount, UINT stride, UINT bufferSize, BYTE* pixels) = 0;
    // Converted to the frame's format; without SetSize or SetPixelFormat, the source's are taken.
    virtual HRESULT STDMETHODCALLTYPE WriteSource(IWICBitmapSource* source, WICRect* rect) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
};

struct IWICBitmapEncoder : public IUnknown {
    LM_IID(0x00000103, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, WICBitmapEncoderCacheOption cacheOption) = 0;
    // One frame. encoderOptions (may be null): the options to fill in and pass to its Initialize.
    virtual HRESULT STDMETHODCALLTYPE CreateNewFrame(IWICBitmapFrameEncode** frame, IPropertyBag2** encoderOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit() = 0;
};

// Stateless: one factory serves any number of threads.
struct IWICImagingFactory : public IUnknown {
    LM_IID(0xec5ec8a9, 0xc395, 0x4314, 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(LPCWSTR fileName, const GUID* vendor, DWORD desiredAccess,
                                                                WICDecodeOptions metadataOptions, IWICBitmapDecoder** decoder) = 0;
    // Reads the whole stream from its start; memory from IWICStream::InitializeFromMemory is read in place.
    virtual HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(IStream* stream, const GUID* vendor, WICDecodeOptions metadataOptions,
                                                              IWICBitmapDecoder** decoder) = 0;
    // A pixel format's IWICPixelFormatInfo.
    virtual HRESULT STDMETHODCALLTYPE CreateComponentInfo(REFCLSID component, IWICComponentInfo** info) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateEncoder(REFGUID containerFormat, const GUID* vendor, IWICBitmapEncoder** encoder) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFormatConverter(IWICFormatConverter** converter) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapScaler(IWICBitmapScaler** scaler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapClipper(IWICBitmapClipper** clipper) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStream(IWICStream** stream) = 0;
    // Zeroed pixels; every cache option behaves alike (the pixels are in memory).
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat,
                                                   WICBitmapCreateCacheOption option, IWICBitmap** bitmap) = 0;
    // A copy of the caller's pixels.
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromMemory(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat, UINT stride,
                                                             UINT bufferSize, BYTE* buffer, IWICBitmap** bitmap) = 0;
};
