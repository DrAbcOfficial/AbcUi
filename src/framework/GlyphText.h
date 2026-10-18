#pragma once

// framework::glyphs - additive HUD ink.
//
// RmlUi always blends text and fills source-over, so a numeral cannot be
// drawn with the additive blend the HUD sprites use (kRenderTransAdd). These
// helpers compose the same glyphs as <img> elements whose source is a
// texadd:// texture - AbcRenderInterface loads that prefix additive - which
// is what makes a readout glow into the scene the way the sprite icons do
// and removes the need for a black outline to stay readable.
//
// The glyph set is a fixed cell (hud/img/glyph/<name>.png, all drawn on the
// cell baseline); health.rcss sizes both axes in em so the ink follows the
// surrounding type. Callers keep a text fallback: markup() returns an empty
// string when a character has no glyph.

#include <string>

namespace framework {

namespace glyphs {

// texadd://-friendly asset root (no scheme prefix).
constexpr const char *kGlyphRoot = "abc/abcui/hud/img/glyph/";

// Additive meter strip (white, alpha kept) for bar fills.
constexpr const char *kMeterUri =
    "texadd://abc/abcui/hud/img/glyph/meter.png";

// True when every character of `text` has an additive glyph.
bool supports(const std::string &text);

// Markup for one glyph, tinted with the RCSS colour string `tint`; empty when
// the character has no glyph.
std::string glyph(char c, const char *tint);

// Markup for a whole string; empty when any character is unsupported.
std::string markup(const std::string &text, const char *tint);

} // namespace glyphs

} // namespace framework
