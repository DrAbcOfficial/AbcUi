#pragma once

// moduleui::bounce - the short "the readout just changed" pop shared by the
// HUD numbers: the classic vitals and magazine readouts and the third-person
// group's health, armour and magazines.
//
// RmlUi starts a transition or an animation only from a definition change, so
// an inline transform write lands as-is; a class-based pop would need a second
// class just to replay on every change. The pop is stepped instead: trigger()
// marks the change, the first advance() after it reports the peak, and every
// later frame reports a shrinking factor until it lands exactly on the rest
// scale and the pulse goes idle. Modules keep the DOM change-gated -
// advance() reports whether the frame has anything to write, so a settled
// readout costs one boolean.
//
// The factor is written as an inline `transform: scale(...)`: a visual scale
// grows the glyphs over their box instead of reflowing the readout line, and
// each module sheet anchors it to the number's alignment edge through
// transform-origin, so the value grows away from its icon.

#include <cstddef>
#include <cstdio>

namespace moduleui::bounce {

// The burst scale on the frame the value changes (128% of the rest size).
inline constexpr float kPeakScale = 1.28f;
// Total pop length in seconds; the readout is back at rest after this.
inline constexpr double kDuration = 0.16;

class Pulse {
public:
  // A changed value starts the pop. A running pop restarts from the peak: a
  // burst of damage and a heal inside one pop must not queue two pops.
  void trigger() {
    active_ = true;
    started_ = false;
    elapsed_ = 0.0;
  }

  // Advances the pop by one frame. Returns false once the pop is over; while
  // it returns true `scale` holds this frame's factor - the peak on the
  // first call after trigger() (that call consumes no time, so the burst
  // lands on the same frame as the new value), then an ease-out decay.
  bool advance(double frameTime, float &scale) {
    if (!active_)
      return false;
    if (!started_) {
      started_ = true;
      scale = kPeakScale;
      return true;
    }
    if (frameTime > 0.0)
      elapsed_ += frameTime;
    const double progress = elapsed_ / kDuration;
    if (!(progress < 1.0)) { // also rejects NaN
      active_ = false;
      scale = 1.0f;
      return true;
    }
    // Quadratic ease-out: the scale drops fastest right after the burst and
    // settles into the rest scale.
    const float rest = static_cast<float>(1.0 - progress);
    scale = 1.0f + (kPeakScale - 1.0f) * rest * rest;
    return true;
  }

  [[nodiscard]] bool active() const {
    return active_;
  }

  void reset() {
    active_ = false;
    started_ = false;
    elapsed_ = 0.0;
  }

private:
  bool active_ = false;
  bool started_ = false;
  double elapsed_ = 0.0;
};

// The inline transform of a settled readout: "none" instead of "scale(1)"
// keeps the element out of RmlUi's transform state between pops.
inline constexpr const char *kRestTransform = "none";

// "scale(<factor>)" into caller storage - the inline transform the modules
// write. A settled factor (1) becomes kRestTransform, so the finishing frame
// drops the transform it started.
inline const char *transformText(float scale, char *out, std::size_t size) {
  if (!(scale > 1.0f)) { // settled (or NaN): nothing to transform
    std::snprintf(out, size, "%s", kRestTransform);
    return out;
  }
  std::snprintf(out, size, "scale(%.3f)", scale);
  return out;
}

} // namespace moduleui::bounce
