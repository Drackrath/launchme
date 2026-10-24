// The marks drawn from SVG path data (src/ui/brand_icons.cpp): every store/console mark and launchme's own logo
// parse, the logo spans its SVG box and its holes (the portal's arch, the letters' counters) stay holes.
#include "../src/ui/brand_icons.hpp"

#include <d2d1.h>
#include <wrl/client.h>

#include <cmath>
#include <iostream>

int main() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return 1;
    for (const char* key : {"steam", "gog", "epic", "battlenet", "ea", "ubisoft", "playstation", "commodore", "retroarch", "wii", "wiiu",
                            "gamecube", "windows", "xbox", "switch"}) {
        const auto* icon = brand_icons::find(key);
        ComPtr<ID2D1Geometry> geometry;
        if (!icon || FAILED(brand_icons::build(factory.Get(), *icon, geometry.GetAddressOf())) || !geometry) {
            std::cerr << "brand icon " << key << " does not build\n";
            return 2;
        }
    }
    ComPtr<ID2D1Geometry> logo;
    if (FAILED(brand_icons::buildLogo(factory.Get(), logo.GetAddressOf())) || !logo) return 3;
    D2D1_RECT_F bounds{};
    if (FAILED(logo->GetBounds(nullptr, &bounds))) return 4;
    // res/launchme_logo.svg: the symbol from (28, 28) to (228, 228), the wordmark's "e" ending near x = 1052.
    if (std::fabs(bounds.left - 28) > 0.5f || std::fabs(bounds.top - 28) > 0.5f || std::fabs(bounds.bottom - 228) > 0.5f ||
        bounds.right < 1050 || bounds.right > 1054) {
        std::cerr << "logo bounds " << bounds.left << ' ' << bounds.top << ' ' << bounds.right << ' ' << bounds.bottom << '\n';
        return 5;
    }
    const auto filled = [&](float x, float y) {
        BOOL inside = FALSE;
        logo->FillContainsPoint(D2D1::Point2F(x, y), nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &inside);
        return inside != FALSE;
    };
    // The portal: its left leg and arch filled, the doorway and the shifted threshold's step empty; the "l"'s stem
    // filled, the counter of the "a" and the inside of the "o"-like "e" bowl empty.
    if (!filled(50, 200) || !filled(128, 50) || !filled(100, 200) || filled(128, 128) || filled(150, 200) || !filled(298, 120) ||
        filled(362, 157) || !filled(1010, 135) || filled(1010, 115)) return 6;
    return 0;
}
