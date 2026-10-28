#pragma once
// Linux only: the Direct2D 1.2 helpers (as the Windows SDK's d2d1_2helper.h).
#include "d2d1_2.h"

namespace D2D1 {
inline FLOAT ComputeFlatteningTolerance(const D2D1_MATRIX_3X2_F& matrix, FLOAT dpiX = 96.0f, FLOAT dpiY = 96.0f, FLOAT maxZoomFactor = 1.0f) {
    const D2D1_MATRIX_3X2_F dpiDependentTransform = matrix * D2D1::Matrix3x2F::Scale(dpiX / 96.0f, dpiY / 96.0f);
    const FLOAT absMaxZoomFactor = maxZoomFactor > 0 ? maxZoomFactor : -maxZoomFactor;
    return D2D1_DEFAULT_FLATTENING_TOLERANCE / (absMaxZoomFactor * D2D1ComputeMaximumScaleFactor(&dpiDependentTransform));
}
} // namespace D2D1
