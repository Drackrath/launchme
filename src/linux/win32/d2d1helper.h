#pragma once
// Linux only: the D2D1 helper namespace (as the Windows SDK's d2d1helper.h): value builders,
// ColorF and Matrix3x2F.
#include "d2d1.h"

namespace D2D1 {
inline D2D1_MATRIX_3X2_F IdentityMatrix();

inline D2D1_POINT_2F Point2F(FLOAT x = 0.f, FLOAT y = 0.f) { return {x, y}; }
inline D2D1_POINT_2U Point2U(UINT32 x = 0, UINT32 y = 0) { return {x, y}; }
inline D2D1_SIZE_F SizeF(FLOAT width = 0.f, FLOAT height = 0.f) { return {width, height}; }
inline D2D1_SIZE_U SizeU(UINT32 width = 0, UINT32 height = 0) { return {width, height}; }
inline D2D1_RECT_F RectF(FLOAT left = 0.f, FLOAT top = 0.f, FLOAT right = 0.f, FLOAT bottom = 0.f) { return {left, top, right, bottom}; }
inline D2D1_RECT_U RectU(UINT32 left = 0, UINT32 top = 0, UINT32 right = 0, UINT32 bottom = 0) { return {left, top, right, bottom}; }
inline D2D1_RECT_F InfiniteRect() { return {-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX}; }

inline D2D1_ARC_SEGMENT ArcSegment(const D2D1_POINT_2F& point, const D2D1_SIZE_F& size, FLOAT rotationAngle,
                                   D2D1_SWEEP_DIRECTION sweepDirection, D2D1_ARC_SIZE arcSize) {
    return {point, size, rotationAngle, sweepDirection, arcSize};
}
inline D2D1_BEZIER_SEGMENT BezierSegment(const D2D1_POINT_2F& point1, const D2D1_POINT_2F& point2, const D2D1_POINT_2F& point3) {
    return {point1, point2, point3};
}
inline D2D1_QUADRATIC_BEZIER_SEGMENT QuadraticBezierSegment(const D2D1_POINT_2F& point1, const D2D1_POINT_2F& point2) {
    return {point1, point2};
}
inline D2D1_ELLIPSE Ellipse(const D2D1_POINT_2F& center, FLOAT radiusX, FLOAT radiusY) { return {center, radiusX, radiusY}; }
inline D2D1_ROUNDED_RECT RoundedRect(const D2D1_RECT_F& rect, FLOAT radiusX, FLOAT radiusY) { return {rect, radiusX, radiusY}; }
inline D2D1_BRUSH_PROPERTIES BrushProperties(FLOAT opacity = 1.0f, const D2D1_MATRIX_3X2_F& transform = D2D1::IdentityMatrix()) {
    return {opacity, transform};
}
inline D2D1_GRADIENT_STOP GradientStop(FLOAT position, const D2D1_COLOR_F& color) { return {position, color}; }
inline D2D1_STROKE_STYLE_PROPERTIES StrokeStyleProperties(D2D1_CAP_STYLE startCap = D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE endCap = D2D1_CAP_STYLE_FLAT,
                                                          D2D1_CAP_STYLE dashCap = D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN lineJoin = D2D1_LINE_JOIN_MITER,
                                                          FLOAT miterLimit = 10.0f, D2D1_DASH_STYLE dashStyle = D2D1_DASH_STYLE_SOLID,
                                                          FLOAT dashOffset = 0.0f) {
    return {startCap, endCap, dashCap, lineJoin, miterLimit, dashStyle, dashOffset};
}
inline D2D1_BITMAP_BRUSH_PROPERTIES BitmapBrushProperties(D2D1_EXTEND_MODE extendModeX = D2D1_EXTEND_MODE_CLAMP,
                                                          D2D1_EXTEND_MODE extendModeY = D2D1_EXTEND_MODE_CLAMP,
                                                          D2D1_BITMAP_INTERPOLATION_MODE interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR) {
    return {extendModeX, extendModeY, interpolationMode};
}
inline D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES LinearGradientBrushProperties(const D2D1_POINT_2F& startPoint, const D2D1_POINT_2F& endPoint) {
    return {startPoint, endPoint};
}
inline D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES RadialGradientBrushProperties(const D2D1_POINT_2F& center, const D2D1_POINT_2F& gradientOriginOffset,
                                                                           FLOAT radiusX, FLOAT radiusY) {
    return {center, gradientOriginOffset, radiusX, radiusY};
}
inline D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_UNKNOWN) {
    return {dxgiFormat, alphaMode};
}
inline D2D1_BITMAP_PROPERTIES BitmapProperties(const D2D1_PIXEL_FORMAT& pixelFormat = D2D1::PixelFormat(), FLOAT dpiX = 96.0f, FLOAT dpiY = 96.0f) {
    return {pixelFormat, dpiX, dpiY};
}
inline D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties(D2D1_RENDER_TARGET_TYPE type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                            const D2D1_PIXEL_FORMAT& pixelFormat = D2D1::PixelFormat(), FLOAT dpiX = 0.0f,
                                                            FLOAT dpiY = 0.0f, D2D1_RENDER_TARGET_USAGE usage = D2D1_RENDER_TARGET_USAGE_NONE,
                                                            D2D1_FEATURE_LEVEL minLevel = D2D1_FEATURE_LEVEL_DEFAULT) {
    return {type, pixelFormat, dpiX, dpiY, usage, minLevel};
}
inline D2D1_HWND_RENDER_TARGET_PROPERTIES HwndRenderTargetProperties(HWND hwnd, D2D1_SIZE_U pixelSize = D2D1::SizeU(0, 0),
                                                                     D2D1_PRESENT_OPTIONS presentOptions = D2D1_PRESENT_OPTIONS_NONE) {
    return {hwnd, pixelSize, presentOptions};
}
inline D2D1_LAYER_PARAMETERS LayerParameters(const D2D1_RECT_F& contentBounds = D2D1::InfiniteRect(), ID2D1Geometry* geometricMask = nullptr,
                                             D2D1_ANTIALIAS_MODE maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                             D2D1_MATRIX_3X2_F maskTransform = D2D1::IdentityMatrix(), FLOAT opacity = 1.0f,
                                             ID2D1Brush* opacityBrush = nullptr, D2D1_LAYER_OPTIONS layerOptions = D2D1_LAYER_OPTIONS_NONE) {
    return {contentBounds, geometricMask, maskAntialiasMode, maskTransform, opacity, opacityBrush, layerOptions};
}

class ColorF : public D2D1_COLOR_F {
public:
    enum Enum {
        AliceBlue = 0xF0F8FF, AntiqueWhite = 0xFAEBD7, Aqua = 0x00FFFF, Aquamarine = 0x7FFFD4, Azure = 0xF0FFFF, Beige = 0xF5F5DC,
        Bisque = 0xFFE4C4, Black = 0x000000, BlanchedAlmond = 0xFFEBCD, Blue = 0x0000FF, BlueViolet = 0x8A2BE2, Brown = 0xA52A2A,
        BurlyWood = 0xDEB887, CadetBlue = 0x5F9EA0, Chartreuse = 0x7FFF00, Chocolate = 0xD2691E, Coral = 0xFF7F50,
        CornflowerBlue = 0x6495ED, Cornsilk = 0xFFF8DC, Crimson = 0xDC143C, Cyan = 0x00FFFF, DarkBlue = 0x00008B, DarkCyan = 0x008B8B,
        DarkGoldenrod = 0xB8860B, DarkGray = 0xA9A9A9, DarkGreen = 0x006400, DarkKhaki = 0xBDB76B, DarkMagenta = 0x8B008B,
        DarkOliveGreen = 0x556B2F, DarkOrange = 0xFF8C00, DarkOrchid = 0x9932CC, DarkRed = 0x8B0000, DarkSalmon = 0xE9967A,
        DarkSeaGreen = 0x8FBC8F, DarkSlateBlue = 0x483D8B, DarkSlateGray = 0x2F4F4F, DarkTurquoise = 0x00CED1, DarkViolet = 0x9400D3,
        DeepPink = 0xFF1493, DeepSkyBlue = 0x00BFFF, DimGray = 0x696969, DodgerBlue = 0x1E90FF, Firebrick = 0xB22222,
        FloralWhite = 0xFFFAF0, ForestGreen = 0x228B22, Fuchsia = 0xFF00FF, Gainsboro = 0xDCDCDC, GhostWhite = 0xF8F8FF, Gold = 0xFFD700,
        Goldenrod = 0xDAA520, Gray = 0x808080, Green = 0x008000, GreenYellow = 0xADFF2F, Honeydew = 0xF0FFF0, HotPink = 0xFF69B4,
        IndianRed = 0xCD5C5C, Indigo = 0x4B0082, Ivory = 0xFFFFF0, Khaki = 0xF0E68C, Lavender = 0xE6E6FA, LavenderBlush = 0xFFF0F5,
        LawnGreen = 0x7CFC00, LemonChiffon = 0xFFFACD, LightBlue = 0xADD8E6, LightCoral = 0xF08080, LightCyan = 0xE0FFFF,
        LightGoldenrodYellow = 0xFAFAD2, LightGreen = 0x90EE90, LightGray = 0xD3D3D3, LightPink = 0xFFB6C1, LightSalmon = 0xFFA07A,
        LightSeaGreen = 0x20B2AA, LightSkyBlue = 0x87CEFA, LightSlateGray = 0x778899, LightSteelBlue = 0xB0C4DE, LightYellow = 0xFFFFE0,
        Lime = 0x00FF00, LimeGreen = 0x32CD32, Linen = 0xFAF0E6, Magenta = 0xFF00FF, Maroon = 0x800000, MediumAquamarine = 0x66CDAA,
        MediumBlue = 0x0000CD, MediumOrchid = 0xBA55D3, MediumPurple = 0x9370DB, MediumSeaGreen = 0x3CB371, MediumSlateBlue = 0x7B68EE,
        MediumSpringGreen = 0x00FA9A, MediumTurquoise = 0x48D1CC, MediumVioletRed = 0xC71585, MidnightBlue = 0x191970,
        MintCream = 0xF5FFFA, MistyRose = 0xFFE4E1, Moccasin = 0xFFE4B5, NavajoWhite = 0xFFDEAD, Navy = 0x000080, OldLace = 0xFDF5E6,
        Olive = 0x808000, OliveDrab = 0x6B8E23, Orange = 0xFFA500, OrangeRed = 0xFF4500, Orchid = 0xDA70D6, PaleGoldenrod = 0xEEE8AA,
        PaleGreen = 0x98FB98, PaleTurquoise = 0xAFEEEE, PaleVioletRed = 0xDB7093, PapayaWhip = 0xFFEFD5, PeachPuff = 0xFFDAB9,
        Peru = 0xCD853F, Pink = 0xFFC0CB, Plum = 0xDDA0DD, PowderBlue = 0xB0E0E6, Purple = 0x800080, Red = 0xFF0000,
        RosyBrown = 0xBC8F8F, RoyalBlue = 0x4169E1, SaddleBrown = 0x8B4513, Salmon = 0xFA8072, SandyBrown = 0xF4A460,
        SeaGreen = 0x2E8B57, SeaShell = 0xFFF5EE, Sienna = 0xA0522D, Silver = 0xC0C0C0, SkyBlue = 0x87CEEB, SlateBlue = 0x6A5ACD,
        SlateGray = 0x708090, Snow = 0xFFFAFA, SpringGreen = 0x00FF7F, SteelBlue = 0x4682B4, Tan = 0xD2B48C, Teal = 0x008080,
        Thistle = 0xD8BFD8, Tomato = 0xFF6347, Turquoise = 0x40E0D0, Violet = 0xEE82EE, Wheat = 0xF5DEB3, White = 0xFFFFFF,
        WhiteSmoke = 0xF5F5F5, Yellow = 0xFFFF00, YellowGreen = 0x9ACD32,
    };
    ColorF(UINT32 rgb, FLOAT alpha = 1.0f) { init(rgb, alpha); }
    ColorF(Enum knownColor, FLOAT alpha = 1.0f) { init(knownColor, alpha); }
    ColorF(FLOAT red, FLOAT green, FLOAT blue, FLOAT alpha = 1.0f) { r = red; g = green; b = blue; a = alpha; }
private:
    void init(UINT32 rgb, FLOAT alpha) {
        r = static_cast<FLOAT>((rgb >> 16) & 0xff) / 255.f;
        g = static_cast<FLOAT>((rgb >> 8) & 0xff) / 255.f;
        b = static_cast<FLOAT>(rgb & 0xff) / 255.f;
        a = alpha;
    }
};

class Matrix3x2F : public D2D1_MATRIX_3X2_F {
public:
    Matrix3x2F(FLOAT m11, FLOAT m12, FLOAT m21, FLOAT m22, FLOAT m31, FLOAT m32) {
        _11 = m11; _12 = m12; _21 = m21; _22 = m22; _31 = m31; _32 = m32;
    }
    Matrix3x2F() = default;

    static Matrix3x2F Identity() { return {1, 0, 0, 1, 0, 0}; }
    static Matrix3x2F Translation(D2D1_SIZE_F size) { return {1, 0, 0, 1, size.width, size.height}; }
    static Matrix3x2F Translation(FLOAT x, FLOAT y) { return Translation(SizeF(x, y)); }
    static Matrix3x2F Scale(D2D1_SIZE_F size, D2D1_POINT_2F center = D2D1::Point2F()) {
        return {size.width, 0, 0, size.height, center.x - size.width * center.x, center.y - size.height * center.y};
    }
    static Matrix3x2F Scale(FLOAT x, FLOAT y, D2D1_POINT_2F center = D2D1::Point2F()) { return Scale(SizeF(x, y), center); }
    static Matrix3x2F Rotation(FLOAT angle, D2D1_POINT_2F center = D2D1::Point2F()) {
        Matrix3x2F rotation;
        D2D1MakeRotateMatrix(angle, center, &rotation);
        return rotation;
    }
    static Matrix3x2F Skew(FLOAT angleX, FLOAT angleY, D2D1_POINT_2F center = D2D1::Point2F()) {
        Matrix3x2F skew;
        D2D1MakeSkewMatrix(angleX, angleY, center, &skew);
        return skew;
    }
    static const Matrix3x2F* ReinterpretBaseType(const D2D1_MATRIX_3X2_F* matrix) { return static_cast<const Matrix3x2F*>(matrix); }
    static Matrix3x2F* ReinterpretBaseType(D2D1_MATRIX_3X2_F* matrix) { return static_cast<Matrix3x2F*>(matrix); }

    FLOAT Determinant() const { return _11 * _22 - _12 * _21; }
    bool IsInvertible() const { return !!D2D1IsMatrixInvertible(this); }
    bool Invert() { return !!D2D1InvertMatrix(this); }
    bool IsIdentity() const { return _11 == 1.f && _12 == 0.f && _21 == 0.f && _22 == 1.f && _31 == 0.f && _32 == 0.f; }
    void SetProduct(const Matrix3x2F& a, const Matrix3x2F& b) {
        const Matrix3x2F result{a._11 * b._11 + a._12 * b._21, a._11 * b._12 + a._12 * b._22,
                                a._21 * b._11 + a._22 * b._21, a._21 * b._12 + a._22 * b._22,
                                a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32};
        *this = result;
    }
    Matrix3x2F operator*(const Matrix3x2F& matrix) const {
        Matrix3x2F result;
        result.SetProduct(*this, matrix);
        return result;
    }
    D2D1_POINT_2F TransformPoint(D2D1_POINT_2F point) const {
        return {point.x * _11 + point.y * _21 + _31, point.x * _12 + point.y * _22 + _32};
    }
};

inline D2D1_POINT_2F operator*(const D2D1_POINT_2F& point, const D2D1_MATRIX_3X2_F& matrix) {
    return Matrix3x2F::ReinterpretBaseType(&matrix)->TransformPoint(point);
}
inline D2D1_MATRIX_3X2_F IdentityMatrix() { return Matrix3x2F::Identity(); }
} // namespace D2D1

inline D2D1_MATRIX_3X2_F operator*(const D2D1_MATRIX_3X2_F& matrix1, const D2D1_MATRIX_3X2_F& matrix2) {
    return *D2D1::Matrix3x2F::ReinterpretBaseType(&matrix1) * *D2D1::Matrix3x2F::ReinterpretBaseType(&matrix2);
}
inline bool operator==(const D2D1_SIZE_U& size1, const D2D1_SIZE_U& size2) { return size1.width == size2.width && size1.height == size2.height; }
inline bool operator==(const D2D1_RECT_U& rect1, const D2D1_RECT_U& rect2) {
    return rect1.left == rect2.left && rect1.top == rect2.top && rect1.right == rect2.right && rect1.bottom == rect2.bottom;
}
