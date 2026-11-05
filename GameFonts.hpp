#pragma once

#include "Font.hpp"
#include "Load.hpp"

// Patrick Hand (SIL OFL 1.1, see dist/README-PatrickHand.txt) at two raster sizes,
// since the atlas has no mipmaps and looks rough when scaled down a long way.
extern Load< Font > ui_font;
extern Load< Font > title_font;

// Distance from the baseline to the visual middle of capital letters, as a fraction of text height.
constexpr float kFontCapCenter = 0.33f;
