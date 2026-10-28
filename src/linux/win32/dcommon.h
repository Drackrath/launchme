#pragma once
// Linux only: the plain types Direct2D and DirectWrite share (as the Windows SDK's dcommon.h
// and d2dbasetypes.h), with the Windows SDK's names and layouts.
#include "win32_base.hpp"
#include "dxgiformat.h"

enum DWRITE_MEASURING_MODE { DWRITE_MEASURING_MODE_NATURAL = 0, DWRITE_MEASURING_MODE_GDI_CLASSIC = 1, DWRITE_MEASURING_MODE_GDI_NATURAL = 2 };
enum D2D1_ALPHA_MODE : unsigned {
    D2D1_ALPHA_MODE_UNKNOWN = 0, D2D1_ALPHA_MODE_PREMULTIPLIED = 1, D2D1_ALPHA_MODE_STRAIGHT = 2, D2D1_ALPHA_MODE_IGNORE = 3,
};
struct D2D1_PIXEL_FORMAT { DXGI_FORMAT format; D2D1_ALPHA_MODE alphaMode; };

struct D2D_POINT_2U { UINT32 x, y; };
struct D2D_POINT_2F { FLOAT x, y; };
struct D2D_POINT_2L { LONG x, y; };
struct D2D_VECTOR_2F { FLOAT x, y; };
struct D2D_VECTOR_3F { FLOAT x, y, z; };
struct D2D_VECTOR_4F { FLOAT x, y, z, w; };
struct D2D_RECT_F { FLOAT left, top, right, bottom; };
struct D2D_RECT_U { UINT32 left, top, right, bottom; };
struct D2D_RECT_L { LONG left, top, right, bottom; };
struct D2D_SIZE_F { FLOAT width, height; };
struct D2D_SIZE_U { UINT32 width, height; };
struct D2D_COLOR_F { FLOAT r, g, b, a; };
struct D2D_MATRIX_3X2_F {
    union {
        struct { FLOAT m11, m12, m21, m22, dx, dy; };
        struct { FLOAT _11, _12, _21, _22, _31, _32; };
        FLOAT m[3][2];
    };
};
struct D2D_MATRIX_4X3_F { union { struct { FLOAT _11, _12, _13, _21, _22, _23, _31, _32, _33, _41, _42, _43; }; FLOAT m[4][3]; }; };
struct D2D_MATRIX_4X4_F {
    union {
        struct { FLOAT _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44; };
        FLOAT m[4][4];
    };
};
struct D2D_MATRIX_5X4_F {
    union {
        struct { FLOAT _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44, _51, _52, _53, _54; };
        FLOAT m[5][4];
    };
};
