#pragma once
// Linux only: Direct2D 1.1 (as the Windows SDK's d2d1_1.h): effects, ID2D1Bitmap1 and the device
// context every render target of this port also is.
#include "d2d1.h"
#include "d2d1effects.h"

struct ID2D1ColorContext;

enum D2D1_PROPERTY_TYPE : UINT32 {
    D2D1_PROPERTY_TYPE_UNKNOWN = 0, D2D1_PROPERTY_TYPE_STRING = 1, D2D1_PROPERTY_TYPE_BOOL = 2, D2D1_PROPERTY_TYPE_UINT32 = 3,
    D2D1_PROPERTY_TYPE_INT32 = 4, D2D1_PROPERTY_TYPE_FLOAT = 5, D2D1_PROPERTY_TYPE_VECTOR2 = 6, D2D1_PROPERTY_TYPE_VECTOR3 = 7,
    D2D1_PROPERTY_TYPE_VECTOR4 = 8, D2D1_PROPERTY_TYPE_BLOB = 9, D2D1_PROPERTY_TYPE_IUNKNOWN = 10, D2D1_PROPERTY_TYPE_ENUM = 11,
    D2D1_PROPERTY_TYPE_ARRAY = 12, D2D1_PROPERTY_TYPE_CLSID = 13, D2D1_PROPERTY_TYPE_MATRIX_3X2 = 14, D2D1_PROPERTY_TYPE_MATRIX_4X3 = 15,
    D2D1_PROPERTY_TYPE_MATRIX_4X4 = 16, D2D1_PROPERTY_TYPE_MATRIX_5X4 = 17, D2D1_PROPERTY_TYPE_COLOR_CONTEXT = 18,
};
enum D2D1_BITMAP_OPTIONS : UINT32 {
    D2D1_BITMAP_OPTIONS_NONE = 0, D2D1_BITMAP_OPTIONS_TARGET = 1, D2D1_BITMAP_OPTIONS_CANNOT_DRAW = 2,
    D2D1_BITMAP_OPTIONS_CPU_READ = 4, D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE = 8,
};
enum D2D1_COMPOSITE_MODE : UINT32 {
    D2D1_COMPOSITE_MODE_SOURCE_OVER = 0, D2D1_COMPOSITE_MODE_DESTINATION_OVER = 1, D2D1_COMPOSITE_MODE_SOURCE_IN = 2,
    D2D1_COMPOSITE_MODE_DESTINATION_IN = 3, D2D1_COMPOSITE_MODE_SOURCE_OUT = 4, D2D1_COMPOSITE_MODE_DESTINATION_OUT = 5,
    D2D1_COMPOSITE_MODE_SOURCE_ATOP = 6, D2D1_COMPOSITE_MODE_DESTINATION_ATOP = 7, D2D1_COMPOSITE_MODE_XOR = 8,
    D2D1_COMPOSITE_MODE_PLUS = 9, D2D1_COMPOSITE_MODE_SOURCE_COPY = 10, D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY = 11,
    D2D1_COMPOSITE_MODE_MASK_INVERT = 12,
};
enum D2D1_INTERPOLATION_MODE : UINT32 {
    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0, D2D1_INTERPOLATION_MODE_LINEAR = 1, D2D1_INTERPOLATION_MODE_CUBIC = 2,
    D2D1_INTERPOLATION_MODE_MULTI_SAMPLE_LINEAR = 3, D2D1_INTERPOLATION_MODE_ANISOTROPIC = 4, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC = 5,
};
enum D2D1_PRIMITIVE_BLEND : UINT32 {
    D2D1_PRIMITIVE_BLEND_SOURCE_OVER = 0, D2D1_PRIMITIVE_BLEND_COPY = 1, D2D1_PRIMITIVE_BLEND_MIN = 2, D2D1_PRIMITIVE_BLEND_ADD = 3,
    D2D1_PRIMITIVE_BLEND_MAX = 4,
};
enum D2D1_LAYER_OPTIONS1 : UINT32 {
    D2D1_LAYER_OPTIONS1_NONE = 0, D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND = 1, D2D1_LAYER_OPTIONS1_IGNORE_ALPHA = 2,
};

struct D2D1_BITMAP_PROPERTIES1 {
    D2D1_PIXEL_FORMAT pixelFormat; FLOAT dpiX; FLOAT dpiY; D2D1_BITMAP_OPTIONS bitmapOptions; ID2D1ColorContext* colorContext;
};
struct D2D1_LAYER_PARAMETERS1 {
    D2D1_RECT_F contentBounds; ID2D1Geometry* geometricMask; D2D1_ANTIALIAS_MODE maskAntialiasMode; D2D1_MATRIX_3X2_F maskTransform;
    FLOAT opacity; ID2D1Brush* opacityBrush; D2D1_LAYER_OPTIONS1 layerOptions;
};

// Effect properties by index. Values are passed as raw bytes; the templates take any value type.
struct ID2D1Properties : public IUnknown {
    LM_IID(0x483473d7, 0xcd46, 0x4f9d, 0x9d, 0x3a, 0x31, 0x12, 0xaa, 0x80, 0x15, 0x9d);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE SetValue(UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE* data, UINT32 dataSize) = 0;

    HRESULT SetValue(UINT32 index, const BYTE* data, UINT32 dataSize) { return SetValue(index, D2D1_PROPERTY_TYPE_UNKNOWN, data, dataSize); }
    template <typename U>
    HRESULT SetValue(U index, const BYTE* data, UINT32 dataSize) { return SetValue(static_cast<UINT32>(index), data, dataSize); }
    template <typename T, typename U>
    HRESULT SetValue(U index, const T& value) {
        return SetValue(static_cast<UINT32>(index), reinterpret_cast<const BYTE*>(&value), static_cast<UINT32>(sizeof(value)));
    }
};

struct ID2D1Effect : public ID2D1Properties {
    LM_IID(0x28211a43, 0x7d89, 0x476f, 0x81, 0x81, 0x2d, 0x61, 0x59, 0xb2, 0x20, 0xad);
    using Base = ID2D1Properties;
    virtual void STDMETHODCALLTYPE SetInput(UINT32 index, ID2D1Image* input, BOOL invalidate = TRUE) = 0;
    virtual void STDMETHODCALLTYPE GetOutput(ID2D1Image** outputImage) const = 0;

    void SetInputEffect(UINT32 index, ID2D1Effect* inputEffect, BOOL invalidate = TRUE) {
        ID2D1Image* output = nullptr;
        if (inputEffect) inputEffect->GetOutput(&output);
        SetInput(index, output, invalidate);
        if (output) output->Release();
    }
};

struct ID2D1Bitmap1 : public ID2D1Bitmap {
    LM_IID(0xa898a84c, 0x3873, 0x4588, 0xb0, 0x8b, 0xeb, 0xbf, 0x97, 0x8d, 0xf0, 0x41);
    using Base = ID2D1Bitmap;
};

struct ID2D1DeviceContext : public ID2D1RenderTarget {
    LM_IID(0xe8f7fe7a, 0x191c, 0x466d, 0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98);
    using Base = ID2D1RenderTarget;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* sourceData, UINT32 pitch,
                                                   const D2D1_BITMAP_PROPERTIES1* bitmapProperties, ID2D1Bitmap1** bitmap) = 0;
    using ID2D1RenderTarget::CreateBitmap;
    virtual HRESULT STDMETHODCALLTYPE CreateEffect(REFCLSID effectId, ID2D1Effect** effect) = 0;
    // False for formats this port cannot sample, block-compressed ones among them.
    virtual BOOL STDMETHODCALLTYPE IsDxgiFormatSupported(DXGI_FORMAT format) const = 0;
    virtual void STDMETHODCALLTYPE SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND primitiveBlend) = 0;
    virtual D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE GetPrimitiveBlend() const = 0;
    virtual void STDMETHODCALLTYPE DrawImage(ID2D1Image* image, const D2D1_POINT_2F* targetOffset = nullptr,
                                             const D2D1_RECT_F* imageRectangle = nullptr,
                                             D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                                             D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) = 0;
    // perspectiveTransform maps (x, y, 0, 1) of the destination rectangle, row vector times matrix, then divides by w.
    virtual void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destinationRectangle, FLOAT opacity,
                                              D2D1_INTERPOLATION_MODE interpolationMode, const D2D1_RECT_F* sourceRectangle = nullptr,
                                              const D2D1_MATRIX_4X4_F* perspectiveTransform = nullptr) = 0;
    using ID2D1RenderTarget::DrawBitmap;
    virtual void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS1* layerParameters, ID2D1Layer* layer) = 0;
    using ID2D1RenderTarget::PushLayer;

    HRESULT CreateBitmap(D2D1_SIZE_U size, const void* sourceData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1& bitmapProperties,
                         ID2D1Bitmap1** bitmap) {
        return CreateBitmap(size, sourceData, pitch, &bitmapProperties, bitmap);
    }
    void DrawImage(ID2D1Effect* effect, const D2D1_POINT_2F* targetOffset = nullptr, const D2D1_RECT_F* imageRectangle = nullptr,
                   D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        ID2D1Image* output = nullptr;
        effect->GetOutput(&output);
        DrawImage(output, targetOffset, imageRectangle, interpolationMode, compositeMode);
        if (output) output->Release();
    }
    void DrawImage(ID2D1Image* image, D2D1_INTERPOLATION_MODE interpolationMode,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(image, nullptr, nullptr, interpolationMode, compositeMode);
    }
    void DrawImage(ID2D1Effect* effect, D2D1_INTERPOLATION_MODE interpolationMode,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(effect, nullptr, nullptr, interpolationMode, compositeMode);
    }
    void DrawImage(ID2D1Image* image, D2D1_POINT_2F targetOffset, D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(image, &targetOffset, nullptr, interpolationMode, compositeMode);
    }
    void DrawImage(ID2D1Effect* effect, D2D1_POINT_2F targetOffset, D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(effect, &targetOffset, nullptr, interpolationMode, compositeMode);
    }
    void DrawImage(ID2D1Image* image, D2D1_POINT_2F targetOffset, const D2D1_RECT_F& imageRectangle,
                   D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(image, &targetOffset, &imageRectangle, interpolationMode, compositeMode);
    }
    void DrawImage(ID2D1Effect* effect, D2D1_POINT_2F targetOffset, const D2D1_RECT_F& imageRectangle,
                   D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR,
                   D2D1_COMPOSITE_MODE compositeMode = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        DrawImage(effect, &targetOffset, &imageRectangle, interpolationMode, compositeMode);
    }
    void PushLayer(const D2D1_LAYER_PARAMETERS1& layerParameters, ID2D1Layer* layer) { PushLayer(&layerParameters, layer); }
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destinationRectangle, FLOAT opacity, D2D1_INTERPOLATION_MODE interpolationMode,
                    const D2D1_RECT_F* sourceRectangle = nullptr, const D2D1_MATRIX_4X4_F* perspectiveTransform = nullptr) {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, sourceRectangle, perspectiveTransform);
    }
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destinationRectangle, FLOAT opacity, D2D1_INTERPOLATION_MODE interpolationMode,
                    const D2D1_RECT_F& sourceRectangle, const D2D1_MATRIX_4X4_F* perspectiveTransform = nullptr) {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, &sourceRectangle, perspectiveTransform);
    }
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destinationRectangle, FLOAT opacity, D2D1_INTERPOLATION_MODE interpolationMode,
                    const D2D1_RECT_F& sourceRectangle, const D2D1_MATRIX_4X4_F& perspectiveTransform) {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, &sourceRectangle, &perspectiveTransform);
    }
};

#include "d2d1_1helper.h"
