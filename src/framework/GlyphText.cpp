// framework::glyphs implementation - see GlyphText.h.
#include "GlyphText.h"

namespace framework {

namespace glyphs {

namespace {

// Glyph file stem for a character, nullptr when the set has none.
const char *glyphFile(char c) {
  switch (c) {
  case '0':
    return "0";
  case '1':
    return "1";
  case '2':
    return "2";
  case '3':
    return "3";
  case '4':
    return "4";
  case '5':
    return "5";
  case '6':
    return "6";
  case '7':
    return "7";
  case '8':
    return "8";
  case '9':
    return "9";
  case '/':
    return "slash";
  case 'x':
  case 'X':
    return "x";
  default:
    return nullptr;
  }
}

} // namespace

bool supports(const std::string &text) {
  for (const char c : text) {
    if (!glyphFile(c))
      return false;
  }
  return true;
}

std::string glyph(char c, const char *tint) {
  const char *file = glyphFile(c);
  if (!file)
    return {};
  std::string out = "<img class=\"hud-glyph\" src=\"texadd://";
  out += kGlyphRoot;
  out += file;
  out += ".png\" style=\"image-color: ";
  out += tint ? tint : "#ffb000";
  out += "\"/>";
  return out;
}

std::string markup(const std::string &text, const char *tint) {
  std::string out;
  out.reserve(text.size() * 96);
  for (const char c : text) {
    const std::string one = glyph(c, tint);
    if (one.empty())
      return {};
    out += one;
  }
  return out;
}

} // namespace glyphs

} // namespace framework
