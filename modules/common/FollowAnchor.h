#pragma once

// moduleui::follow - the third-person follow anchor solver shared by HUD
// modules that pin readouts to a projected world point (abcui.division).
//
// Pure screen-pixel math: no RmlUi, no engine, no allocation. The caller owns
// the projection (ui->project), the fallback anchor and the measured cluster
// box; this header only answers "where does the cluster sit this frame".
//
// The solver is deliberately stateless about policy: it does not know whether
// the target came from a projection or from a fixed anchor, so the same path
// serves third person, first-person fallback and the mode transitions.

#include <algorithm>
#include <cmath>

namespace moduleui::follow {

struct Limits {
  float Width = 0.0f;  // viewport width, px
  float Height = 0.0f; // viewport height, px
  float Margin = 8.0f; // minimum gap to every viewport edge, px
};

struct State {
  float X = 0.0f;     // applied top-left, px
  float Y = 0.0f;     // applied top-left, px
  bool Valid = false; // no sample yet (and after reset)
};

// Exponential follow time constant (seconds). 0.12 s settles a walking bob
// without lagging a turn; the HUD reads as attached rather than glued.
inline constexpr float kTimeConstant = 0.12f;
// A target jump wider than this fraction of the viewport snaps instead of
// gliding (teleport, respawn, mode switch across the screen).
inline constexpr float kSnapFraction = 0.30f;
// Longest frame delta the smoothing may consume; a stall must not teleport.
// Kept above kTimeConstant so a normal frame always gets the true exponential
// step (a 0.1 s cap would silently shorten tau).
inline constexpr float kMaxStep = 0.25f;

inline void reset(State &state) {
  state.Valid = false;
}

// Clamps one axis so [value, value + box] stays inside [margin, extent -
// margin]. A box larger than the usable extent pins to the low margin.
inline float clampAxis(float value, float box, float extent, float margin) {
  const float low = margin;
  const float high = extent - margin - box;
  if (high < low)
    return low;
  return std::clamp(value, low, high);
}

// Clamps the box top-left into the viewport margins.
inline void clamp(float x, float y, float boxWidth, float boxHeight,
                  const Limits &limits, float out[2]) {
  out[0] = clampAxis(x, boxWidth, limits.Width, limits.Margin);
  out[1] = clampAxis(y, boxHeight, limits.Height, limits.Margin);
}

// Moves `state` toward (x, y) and writes the applied top-left to `out`.
// `snap` (or the first sample after reset) jumps to the clamped target; a
// jump wider than kSnapFraction of the viewport snaps as well. The applied
// position is clamped on the way out too, so a viewport change can never
// leave the cluster outside the margins.
inline void step(State &state, float x, float y, float boxWidth,
                 float boxHeight, const Limits &limits, float dt, bool snap,
                 float out[2]) {
  float desired[2] = {};
  clamp(x, y, boxWidth, boxHeight, limits, desired);

  if (snap || !state.Valid) {
    state.X = desired[0];
    state.Y = desired[1];
    state.Valid = true;
  } else {
    const float stepSeconds = std::clamp(dt, 0.0f, kMaxStep);
    const float dx = desired[0] - state.X;
    const float dy = desired[1] - state.Y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float snapDistance =
        limits.Width > 0.0f ? limits.Width * kSnapFraction : kSnapFraction;
    if (distance > snapDistance) {
      state.X = desired[0];
      state.Y = desired[1];
    } else if (stepSeconds > 0.0f) {
      const float alpha = 1.0f - std::exp(-stepSeconds / kTimeConstant);
      state.X += dx * alpha;
      state.Y += dy * alpha;
    }
  }

  float clamped[2] = {};
  clamp(state.X, state.Y, boxWidth, boxHeight, limits, clamped);
  state.X = clamped[0];
  state.Y = clamped[1];
  out[0] = state.X;
  out[1] = state.Y;
}

} // namespace moduleui::follow
