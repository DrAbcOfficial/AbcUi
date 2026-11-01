#pragma once

// Explicit opacity interpolation for inline HUD values. RmlUi transitions
// apply to stylesheet definition changes, not elementSetProperty writes.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace moduleui::opacity {

inline double frameDelta(double seconds) {
  return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
}

inline float clamp(float value) {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

class Ramp {
public:
  explicit Ramp(float value = 0.0f)
      : Value(clamp(value)), From(Value), Target(Value) {}

  void reset(float value = 0.0f) {
    Value = From = Target = clamp(value);
    Elapsed = Duration = 0.0;
    Dirty = true;
  }

  void retarget(float value, double seconds) {
    value = clamp(value);
    if (value == Target)
      return;
    From = Value;
    Target = value;
    Elapsed = 0.0;
    Duration = frameDelta(seconds);
    if (Duration == 0.0)
      Value = Target;
    Dirty = true;
  }

  // Consume at most one write per changed value. Invalid deltas pause the
  // ramp without poisoning its state; a long finite frame lands at the end.
  bool advance(double seconds, float &value) {
    bool changed = std::exchange(Dirty, false);
    if (Elapsed < Duration) {
      Elapsed = std::min(Duration, Elapsed + frameDelta(seconds));
      const float next =
          From + (Target - From) * static_cast<float>(Elapsed / Duration);
      changed = changed || next != Value;
      Value = next;
    }
    value = Value;
    return changed;
  }

  [[nodiscard]] float value() const {
    return Value;
  }

private:
  float Value;
  float From;
  float Target;
  double Elapsed = 0.0;
  double Duration = 0.0;
  bool Dirty = false;
};

inline void text(float value, char (&out)[16]) {
  std::snprintf(out, sizeof(out), "%.3f", clamp(value));
}

} // namespace moduleui::opacity
