#pragma once
// Linux only: Direct2D 1.2 (as the Windows SDK's d2d1_2.h): geometry realizations, which this port
// keeps as the geometry and its stroke and draws as such.
#include "d2d1_1.h"

#include <algorithm>

struct ID2D1GeometryRealization : public ID2D1Resource {
    LM_IID(0xa16907d7, 0xbc02, 0x4801, 0x99, 0xe8, 0x8c, 0xf7, 0xf4, 0x85, 0xf7, 0x74);
    using Base = ID2D1Resource;
};

struct ID2D1DeviceContext1 : public ID2D1DeviceContext {
    LM_IID(0xd37f57e4, 0x6908, 0x459f, 0xa1, 0x99, 0xe7, 0x2f, 0x24, 0xf7, 0x99, 0x87);
    using Base = ID2D1DeviceContext;
    virtual HRESULT STDMETHODCALLTYPE CreateFilledGeometryRealization(ID2D1Geometry* geometry, FLOAT flatteningTolerance,
                                                                      ID2D1GeometryRealization** geometryRealization) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStrokedGeometryRealization(ID2D1Geometry* geometry, FLOAT flatteningTolerance, FLOAT strokeWidth,
                                                                       ID2D1StrokeStyle* strokeStyle,
                                                                       ID2D1GeometryRealization** geometryRealization) = 0;
    virtual void STDMETHODCALLTYPE DrawGeometryRealization(ID2D1GeometryRealization* geometryRealization, ID2D1Brush* brush) = 0;
};

// The largest factor by which `matrix` stretches any direction (its largest singular value).
inline FLOAT D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F* matrix) {
    const double a = matrix->_11, b = matrix->_12, c = matrix->_21, d = matrix->_22;
    const double sum = a * a + b * b + c * c + d * d, determinant = a * d - b * c;
    const double root = std::sqrt(std::max(0.0, sum * sum - 4 * determinant * determinant));
    return static_cast<FLOAT>(std::sqrt((sum + root) * 0.5));
}

#include "d2d1_2helper.h"
