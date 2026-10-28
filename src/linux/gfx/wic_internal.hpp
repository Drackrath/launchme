#pragma once
// Linux only: the WIC port's internals (wic_*.cpp): its pixel formats and their conversions,
// scaling, and the objects behind the imaging factory.
#include "com_internal.hpp"

#include <cstddef>
#include <vector>

namespace lm_wic {
// ---- Pixel formats and conversion (wic_pixels.cpp) ----------------------------------------
enum class Layout { bgra, rgba, bgrx, bgr, rgb, gray, alpha };
struct Format {
    GUID guid;
    UINT bits;
    Layout layout;
    bool premultiplied; // Colour channels multiplied by alpha.
    bool alpha;         // An alpha channel (the colour channels of 8bppAlpha are black).
    size_t rowBytes(UINT width) const { return (size_t{width} * bits + 7) / 8; }
};
inline constexpr Format formats[]{
    {GUID_WICPixelFormat32bppBGRA, 32, Layout::bgra, false, true},
    {GUID_WICPixelFormat32bppPBGRA, 32, Layout::bgra, true, true},
    {GUID_WICPixelFormat32bppBGR, 32, Layout::bgrx, false, false},
    {GUID_WICPixelFormat32bppRGBA, 32, Layout::rgba, false, true},
    {GUID_WICPixelFormat32bppPRGBA, 32, Layout::rgba, true, true},
    {GUID_WICPixelFormat24bppBGR, 24, Layout::bgr, false, false},
    {GUID_WICPixelFormat24bppRGB, 24, Layout::rgb, false, false},
    {GUID_WICPixelFormat8bppGray, 8, Layout::gray, false, false},
    {GUID_WICPixelFormat8bppAlpha, 8, Layout::alpha, false, true},
};
inline constexpr const Format& format_bgra = formats[0];
inline constexpr const Format& format_pbgra = formats[1];
inline constexpr const Format& format_bgrx = formats[2];
inline constexpr const Format& format_bgr = formats[5];
inline constexpr const Format& format_gray = formats[7];
// Null for a format this port does not have.
const Format* findFormat(const GUID& guid);
const Format* formatOf(IWICBitmapSource* source);

// Pixels in memory, rows `stride` bytes apart. Their owner keeps them while it lives.
struct View {
    const Format* format = nullptr;
    UINT width = 0, height = 0;
    size_t stride = 0;
    const BYTE* data = nullptr;
};
struct Image {
    const Format* format = nullptr;
    UINT width = 0, height = 0;
    size_t stride = 0;
    std::vector<BYTE> bytes;
    View view() const { return {format, width, height, stride, bytes.data()}; }
};
// Zeroed, rows padded to 4 bytes as WIC's bitmaps are. Throws std::bad_alloc.
Image makeImage(const Format& format, UINT width, UINT height);

// This port's bitmap sources, besides IWICBitmapSource: the whole image in memory without a
// copy when it is there (or decoding it now puts it there), in the format the view names, which
// may differ from GetPixelFormat's but holds the same pixels (a JPEG's 24bppBGR frame keeps its
// decoded 32bppBGRA). A null view: read it through CopyPixels.
class Source {
public:
    virtual HRESULT view(View& pixels) = 0;
protected:
    ~Source() = default;
};

// `count` pixels from one format to another.
void convertRow(const Format& from, const BYTE* in, const Format& to, BYTE* out, UINT count);
// CopyPixels' arguments for a width x height image: `area` is `rect` (null: everything) once it
// is inside the image and the buffer holds it.
HRESULT checkCopy(const WICRect* rect, UINT width, UINT height, const Format& format, UINT stride, UINT bufferSize, const BYTE* buffer,
                  WICRect& area);
// `area` (checked) of an image into rows `stride` bytes apart, converted to `to`.
void copyView(const View& pixels, const WICRect& area, const Format& to, UINT stride, BYTE* out);
HRESULT copyConverted(IWICBitmapSource* source, const WICRect& area, const Format& to, UINT stride, BYTE* out);
// The whole source in memory: its own view, or else `image` filled through CopyPixels as `format`.
HRESULT viewOf(IWICBitmapSource* source, const Format& format, Image& image, View& pixels);

// Resamples straight or premultiplied pixels in premultiplied alpha (no colour bleeds from
// transparent pixels) to 32bppPBGRA or, from straight alpha, 32bppBGRA. Throws std::bad_alloc.
Image scale(const View& source, UINT width, UINT height, WICBitmapInterpolationMode mode, bool premultiplied);

// ---- Objects (wic_*.cpp) -------------------------------------------------------------------
HRESULT createImagingFactory(REFIID riid, void** object); // wic_factory.cpp, for CoCreateInstance
HRESULT createDecoder(IStream* stream, IWICBitmapDecoder** decoder); // wic_decoder.cpp
// wic_bitmap.cpp: zeroed, or a copy of `pixels` (rows `stride` apart in `size` bytes).
HRESULT createBitmap(UINT width, UINT height, const Format& format, const BYTE* pixels, UINT stride, UINT size, IWICBitmap** bitmap);
HRESULT createConverter(IWICFormatConverter** converter); // wic_transform.cpp
HRESULT createScaler(IWICBitmapScaler** scaler);
HRESULT createClipper(IWICBitmapClipper** clipper);
HRESULT createEncoder(bool jpeg, IWICBitmapEncoder** encoder); // wic_encoder.cpp
} // namespace lm_wic
