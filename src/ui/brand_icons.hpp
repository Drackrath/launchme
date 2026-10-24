#pragma once

// Small single-colour marks of stores and consoles for desktop mode's sidebar, as SVG path data in a
// 24×24 box. Most come from Simple Icons (CC0, https://simpleicons.org); Windows, Xbox and Switch,
// which Simple Icons no longer carries, are simple marks drawn here; Wii and Wii U are the two halves of its former
// Wii U logo, GameCube the Wikimedia Commons logo in one colour.
#include "../model/game.hpp"

#include <d2d1.h>
#include <string_view>

namespace brand_icons {

struct Icon {
    std::string_view key;
    const char* path;
    bool even_odd = false; // Holes by the even-odd rule (the marks drawn here); Simple Icons use non-zero.
    float left = 0, top = 0, size = 24; // The square of the path that is the icon (a cut-out part of a logo).
};

const Icon* find(std::string_view key);
// The mark of a store, or of an emulated console by its name (emulation_platforms); empty: none.
std::string_view sourceKey(GameSource source);
std::string_view consoleKey(std::wstring_view console);
// Builds the icon's filled geometry (24×24 units); an unparsable path fails with E_INVALIDARG.
HRESULT build(ID2D1Factory* factory, const Icon& icon, ID2D1Geometry** geometry);
// launchme's own full logo (res/launchme_logo.svg): the portal symbol and the wordmark, filled in one colour, in the
// SVG's units (the marks span x 28–1052, y 28–228). The symbol alone is the application icon (tools/launchme_icon.py).
HRESULT buildLogo(ID2D1Factory* factory, ID2D1Geometry** geometry);

} // namespace brand_icons
