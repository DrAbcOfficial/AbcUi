#pragma once

// moduleui::barmotion - the shared smooth value-bar animation (issue #30).
//
// RCSS transitions only run when a class or pseudo-class changes an element's
// definition; an inline property write never starts one (RmlUi documents this
// and ElementStyle::TransitionPropertyChanges only runs from UpdateDefinition).
// A bar therefore cannot animate an inline width. Instead the fill carries a
// percentage width class from the 1% ladder in the base package
// (barmotion.rcss), and the bar box carries the direction class that selects
// the transition:
//
//   bar-fill-N  width: N% of the fill's own box (one segmented cell, the
//               vitals meter track or the flashlight battery well)
//   bar-drop    the value fell - a quick ramp, so damage reads immediately
//   bar-gain    the value rose - a slower ramp, so healing reads as a fill
//   bar-snap    no transition: the first show, a re-show or a reset
//
// Percentages also keep the segmented bar honest when the maximum changes:
// a relayout only rewrites the cells' left/width in dp, the fill's class (and
// therefore its percentage) stays, so nothing on the strip animates.
//
// The tracker is the single place that decides whether to write at all, so a
// readout that repeats the same value never restarts a running transition.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace moduleui::barmotion {

// The RCSS contract shared with modules/base/assets/rcss/barmotion.rcss.
inline constexpr const char *kMotionClass = "bar-motion";
inline constexpr const char *kDropClass = "bar-drop";
inline constexpr const char *kGainClass = "bar-gain";
inline constexpr const char *kSnapClass = "bar-snap";
// The width ladder covers 0..100, i.e. one class per percent.
inline constexpr int kSteps = 100;
// Enough for "bar-motion bar-fill-100 bar-gain".
inline constexpr std::size_t kClassCapacity = 48;

enum class Direction { kNone, kDrop, kGain };

// One write decision for a bar: whether the value changed, and whether the
// new width must land instantly or animate.
struct Step {
  bool write = false;
  bool snap = false;
  Direction direction = Direction::kNone;
};

// 0..100 for a filled fraction; negative, over-full and non-finite inputs
// clamp instead of producing an out-of-range class.
inline int percent(float ratio) {
  if (!(ratio > 0.0f)) // also rejects NaN
    return 0;
  const int rounded = static_cast<int>(
      std::lround(std::min(ratio, 1.0f) * static_cast<float>(kSteps)));
  return std::clamp(rounded, 0, kSteps);
}

// "bar-fill-N" (the width ladder token) into caller storage.
inline const char *widthClass(int percent, char *out, std::size_t size) {
  std::snprintf(out, size, "bar-fill-%d", std::clamp(percent, 0, kSteps));
  return out;
}

// "bar-motion <direction>" - the class attribute of a bar box.
inline const char *motionClasses(Direction direction, bool snap, char *out,
                                 std::size_t size) {
  const char *mode =
      snap ? kSnapClass
           : (direction == Direction::kGain ? kGainClass : kDropClass);
  std::snprintf(out, size, "%s %s", kMotionClass, mode);
  return out;
}

// "<base> bar-fill-N" - one segmented cell's fill, e.g. "fill bar-fill-70".
inline const char *fillClasses(const char *base, int percent, char *out,
                               std::size_t size) {
  if (base && *base)
    std::snprintf(out, size, "%s bar-fill-%d", base,
                  std::clamp(percent, 0, kSteps));
  else
    widthClass(percent, out, size);
  return out;
}

// "bar-motion bar-fill-N <direction>" - a bar whose box is its own fill (the
// classic vitals meter).
inline const char *barClasses(Direction direction, bool snap, int percent,
                              char *out, std::size_t size) {
  const char *mode =
      snap ? kSnapClass
           : (direction == Direction::kGain ? kGainClass : kDropClass);
  std::snprintf(out, size, "%s %s bar-fill-%d", kMotionClass, mode,
                std::clamp(percent, 0, kSteps));
  return out;
}

// The write policy of one bar. A hidden bar keeps no live width (the document
// may be display:none, which stops the frame pump), so the first shown write
// snaps to the current value instead of animating from a stale one.
class Tracker {
public:
  [[nodiscard]] Step observe(int value, bool shown) {
    Step step;
    if (!shown) {
      visible_ = false;
      value_ = kUnknown;
      return step;
    }
    const bool appeared = !visible_;
    const bool changed = value != value_;
    visible_ = true;
    if (!changed && !appeared)
      return step;
    step.write = true;
    step.snap = value_ == kUnknown || appeared;
    if (!step.snap)
      step.direction = value > value_ ? Direction::kGain : Direction::kDrop;
    value_ = value;
    return step;
  }

  void reset() {
    value_ = kUnknown;
    visible_ = false;
  }

private:
  // Every tracked readout (health, armour, battery) is non-negative.
  static constexpr int kUnknown = -1;
  int value_ = kUnknown;
  bool visible_ = false;
};

} // namespace moduleui::barmotion
