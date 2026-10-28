// WIC's imaging factory (CLSID_WICImagingFactory and ...2 through CoCreateInstance) and the
// pixel-format information it describes formats with.
#include "wic_internal.hpp"
#include "../win32/wrl/client.h"

using Microsoft::WRL::ComPtr;

namespace lm_wic {
namespace {
class PixelFormatInfo final : public lm_com::Object<IWICPixelFormatInfo> {
public:
    explicit PixelFormatInfo(const Format& format) : format_(format) {}
    HRESULT STDMETHODCALLTYPE GetBitsPerPixel(UINT* bitsPerPixel) override {
        if (!bitsPerPixel) return E_INVALIDARG;
        *bitsPerPixel = format_.bits;
        return S_OK;
    }
private:
    const Format& format_;
};

class Factory final : public lm_com::Object<IWICImagingFactory> {
public:
    HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(LPCWSTR fileName, const GUID*, DWORD desiredAccess, WICDecodeOptions,
                                                        IWICBitmapDecoder** decoder) override {
        if (!decoder) return E_INVALIDARG;
        *decoder = nullptr;
        if (!(desiredAccess & GENERIC_READ)) return E_INVALIDARG;
        ComPtr<IWICStream> stream;
        HRESULT result = CreateStream(stream.GetAddressOf());
        if (SUCCEEDED(result)) result = stream->InitializeFromFilename(fileName, GENERIC_READ);
        return SUCCEEDED(result) ? createDecoder(stream.Get(), decoder) : result;
    }
    HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(IStream* stream, const GUID*, WICDecodeOptions, IWICBitmapDecoder** decoder) override {
        return createDecoder(stream, decoder);
    }
    HRESULT STDMETHODCALLTYPE CreateComponentInfo(REFCLSID component, IWICComponentInfo** info) override {
        if (!info) return E_INVALIDARG;
        *info = nullptr;
        const Format* format = findFormat(component);
        return format ? lm_com::make<PixelFormatInfo>(info, *format) : WINCODEC_ERR_COMPONENTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE CreateEncoder(REFGUID containerFormat, const GUID*, IWICBitmapEncoder** encoder) override {
        if (!encoder) return E_INVALIDARG;
        *encoder = nullptr;
        if (containerFormat != GUID_ContainerFormatPng && containerFormat != GUID_ContainerFormatJpeg) return WINCODEC_ERR_COMPONENTNOTFOUND;
        return createEncoder(containerFormat == GUID_ContainerFormatJpeg, encoder);
    }
    HRESULT STDMETHODCALLTYPE CreateFormatConverter(IWICFormatConverter** converter) override { return createConverter(converter); }
    HRESULT STDMETHODCALLTYPE CreateBitmapScaler(IWICBitmapScaler** scaler) override { return createScaler(scaler); }
    HRESULT STDMETHODCALLTYPE CreateBitmapClipper(IWICBitmapClipper** clipper) override { return createClipper(clipper); }
    HRESULT STDMETHODCALLTYPE CreateStream(IWICStream** stream) override { return lm_com::make<lm_com::Stream>(stream); }
    HRESULT STDMETHODCALLTYPE CreateBitmap(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat, WICBitmapCreateCacheOption,
                                           IWICBitmap** bitmap) override {
        const Format* format = findFormat(pixelFormat);
        if (!format) { if (bitmap) *bitmap = nullptr; return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT; }
        return createBitmap(width, height, *format, nullptr, 0, 0, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromMemory(UINT width, UINT height, REFWICPixelFormatGUID pixelFormat, UINT stride,
                                                     UINT bufferSize, BYTE* buffer, IWICBitmap** bitmap) override {
        const Format* format = findFormat(pixelFormat);
        if (!format || !buffer) {
            if (bitmap) *bitmap = nullptr;
            return format ? E_INVALIDARG : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        }
        return createBitmap(width, height, *format, buffer, stride, bufferSize, bitmap);
    }
};
} // namespace

HRESULT createImagingFactory(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    Factory* factory = nullptr;
    if (const HRESULT result = lm_com::make<Factory>(&factory); FAILED(result)) return result;
    const HRESULT result = factory->QueryInterface(riid, object);
    factory->Release();
    return result;
}
} // namespace lm_wic
