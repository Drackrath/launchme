#pragma once

#include <d2d1.h>
#include <wrl/client.h>
#include <vector>

// Remove solid foreground silhouettes from a background pass. Painter's order alone
// cannot hide it while the foreground artwork or its whole card is fading in.
inline Microsoft::WRL::ComPtr<ID2D1PathGeometry> occlusionMask(ID2D1Factory* factory, D2D1_RECT_F bounds,
    const std::vector<Microsoft::WRL::ComPtr<ID2D1Geometry>>& foreground) {
    if (!factory || foreground.empty()) return {};
    std::vector<ID2D1Geometry*> shapes;
    shapes.reserve(foreground.size());
    for (const auto& shape : foreground) if (shape) shapes.push_back(shape.Get());
    if (shapes.empty()) return {};
    Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> area;
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> group;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> mask;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreateRectangleGeometry(bounds, area.GetAddressOf())) ||
        FAILED(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, shapes.data(), static_cast<UINT32>(shapes.size()), group.GetAddressOf())) ||
        FAILED(factory->CreatePathGeometry(mask.GetAddressOf())) || FAILED(mask->Open(sink.GetAddressOf()))) return {};
    const auto result = area->CombineWithGeometry(group.Get(), D2D1_COMBINE_MODE_EXCLUDE, D2D1::IdentityMatrix(), sink.Get());
    if (FAILED(sink->Close()) || FAILED(result)) return {};
    return mask;
}
