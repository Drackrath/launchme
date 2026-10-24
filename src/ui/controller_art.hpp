#pragma once
// Drawn controller pictures (ui/controller_art.cpp). Every family is a set of path
// geometries in one 440 x 300 design canvas, so all pictures share one scale and
// centre line in the devices panel. The style follows the white product photos:
// light shell, charcoal sticks/keys, grey engravings, no logos. Model-specific
// exceptions: a dark Steam Controller 2 and the yellow Zero 2 with white keys.
namespace controller_art {
// Paint layers, drawn in this order; one cached geometry each per family.
enum Layer : int {
    under,      // parts behind the shell (paddles), mid grey
    body,       // shell silhouette, light gradient
    well,       // recesses around sticks and pads
    inlay,      // dark face plates, clipped to the shell
    plate,      // light raised surfaces (touchpads, trackpads)
    screen,     // dark glass, light bar
    key_light,  // light buttons
    key_dark,   // charcoal buttons, stick caps, rails
    cap,        // concave stick tops, d-pad facets
    mark,       // engraved strokes on light surfaces (symbols, + and -)
    mark_light, // strokes on dark surfaces
    dot,        // small dark marks (speaker holes, LEDs, star)
    glint,      // small light marks on dark surfaces
    layer_count
};
constexpr float width = 440, height = 300;
}
