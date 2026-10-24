#include "../src/platform/image_validation.hpp"
#include "image_fixture.hpp"
#include <iostream>

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    const auto opaque = image_validation::inspect(pngFixture(40, 64, 64));
    if (!opaque || !opaque->png || opaque->hasTransparency()) return 2;
    auto bytes = pngFixture(40, 64, 64, true);
    const auto alpha = image_validation::inspect(bytes);
    if (!alpha || !alpha->hasTransparency() || alpha->width != 64 || alpha->height != 64) return 3;
    bytes.resize(40);
    if (image_validation::inspect(bytes)) return 4;
    std::cout << "Actual PNG alpha and truncated image validation passed\n";
}
