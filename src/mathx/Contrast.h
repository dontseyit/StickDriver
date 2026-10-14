#pragma once
// -----------------------------------------------------------------------------
//  Which of two text colours can be read on a given background.
//
//  A chip and a toast are filled with a colour the app chose. The text on them
//  takes the colour that reads on that fill, so an app passing a dark colour
//  gets light text rather than dark on dark.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

/// Perceived brightness of an RGB565 colour, 0 to 255. The weights are the
/// usual ones for sRGB, scaled to integers so this costs nothing.
constexpr int luma565(uint16_t c) {
    const int r = ((c >> 11) & 0x1F) * 255 / 31;
    const int g = ((c >> 5) & 0x3F) * 255 / 63;
    const int b = (c & 0x1F) * 255 / 31;
    return (r * 54 + g * 183 + b * 19) >> 8;
}

/// `dark` on a light background, `light` on a dark one. The threshold is 100,
/// under the midpoint. On a mid red such as kErr (luma 116) dark text still
/// reads better.
constexpr uint16_t readableOn(uint16_t bg, uint16_t light, uint16_t dark) {
    return luma565(bg) >= 100 ? dark : light;
}

}  // namespace sd
