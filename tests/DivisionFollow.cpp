// abc_ui_follow_anchor - the third-person follow solver behind the
// abcui.division HUD group. Pure math: margins, first-sample snap, the
// exponential step, the discontinuity snap and the reset contract.
#include "common/FollowAnchor.h"

#include <cmath>
#include "framework/TestHarness.h"

using moduleui::follow::Limits;
using moduleui::follow::State;

namespace {

constexpr float kTolerance = 0.001f;

Limits viewport() {
  Limits limits;
  limits.Width = 1920.0f;
  limits.Height = 1080.0f;
  limits.Margin = 8.0f;
  return limits;
}

} // namespace

int main() {
  // A box that fits inside the margins is left where it is.
  {
    TEST_CASE("clamp-inside");
    float out[2] = {};
    moduleui::follow::clamp(200.0f, 300.0f, 400.0f, 34.0f, viewport(), out);
    CHECK_NEAR(out[0], 200.0f, kTolerance);
    CHECK_NEAR(out[1], 300.0f, kTolerance);
  }

  // Every edge pins the box fully inside, not just its top-left corner.
  {
    TEST_CASE("clamp-edges");
    const Limits limits = viewport();
    float left[2] = {};
    moduleui::follow::clamp(-500.0f, 0.0f, 400.0f, 34.0f, limits, left);
    CHECK_NEAR(left[0], 8.0f, kTolerance);

    float right[2] = {};
    moduleui::follow::clamp(5000.0f, 0.0f, 400.0f, 34.0f, limits, right);
    CHECK_NEAR(right[0], 1920.0f - 8.0f - 400.0f, kTolerance);

    float bottom[2] = {};
    moduleui::follow::clamp(0.0f, 5000.0f, 400.0f, 34.0f, limits, bottom);
    CHECK_NEAR(bottom[1], 1080.0f - 8.0f - 34.0f, kTolerance);
  }

  // A box wider than the usable viewport pins to the top-left margin instead
  // of producing a negative range.
  {
    TEST_CASE("clamp-oversize");
    Limits limits = viewport();
    limits.Width = 100.0f;
    limits.Height = 40.0f;
    float out[2] = {};
    moduleui::follow::clamp(50.0f, 20.0f, 400.0f, 34.0f, limits, out);
    CHECK_NEAR(out[0], limits.Margin, kTolerance);
    CHECK_NEAR(out[1], limits.Margin, kTolerance);
  }

  // The first sample after reset snaps: a HUD that appears must not glide in
  // from wherever the previous map left it.
  {
    TEST_CASE("first-sample-snaps");
    State state;
    float out[2] = {};
    moduleui::follow::step(state, 700.0f, 800.0f, 400.0f, 34.0f, viewport(),
                           0.016f, false, out);
    CHECK_NEAR(out[0], 700.0f, kTolerance);
    CHECK_NEAR(out[1], 800.0f, kTolerance);
    CHECK(state.Valid);
  }

  // One time constant closes 1 - 1/e of the gap.
  {
    TEST_CASE("exponential-step");
    State state;
    state.Valid = true;
    state.X = 100.0f;
    state.Y = 100.0f;
    float out[2] = {};
    moduleui::follow::step(state, 500.0f, 100.0f, 400.0f, 34.0f, viewport(),
                           moduleui::follow::kTimeConstant, false, out);
    CHECK_NEAR(out[0], 100.0f + 400.0f * (1.0f - std::exp(-1.0f)), kTolerance);
  }

  // dt = 0 holds still; a stalled frame is clamped to kMaxStep instead of
  // teleporting the cluster.
  {
    TEST_CASE("dt-clamp");
    State state;
    state.Valid = true;
    state.X = 100.0f;
    state.Y = 100.0f;
    float still[2] = {};
    moduleui::follow::step(state, 300.0f, 100.0f, 400.0f, 34.0f, viewport(),
                           0.0f, false, still);
    CHECK_NEAR(still[0], 100.0f, kTolerance);

    State stalled = state;
    float longFrame[2] = {};
    moduleui::follow::step(stalled, 300.0f, 100.0f, 400.0f, 34.0f, viewport(),
                           5.0f, false, longFrame);
    State capped;
    capped.Valid = true;
    capped.X = 100.0f;
    capped.Y = 100.0f;
    float cappedOut[2] = {};
    moduleui::follow::step(capped, 300.0f, 100.0f, 400.0f, 34.0f, viewport(),
                           moduleui::follow::kMaxStep, false, cappedOut);
    CHECK_NEAR(longFrame[0], cappedOut[0], kTolerance);
  }

  // A jump wider than the snap fraction is applied directly (respawn,
  // teleport, a mode switch to the other side of the screen).
  {
    TEST_CASE("discontinuity-snaps");
    State state;
    state.Valid = true;
    state.X = 100.0f;
    state.Y = 100.0f;
    float out[2] = {};
    moduleui::follow::step(state, 1400.0f, 100.0f, 400.0f, 34.0f, viewport(),
                           0.016f, false, out);
    CHECK_NEAR(out[0], 1400.0f, kTolerance);
  }

  // The explicit snap flag (mode switches, reset, map change) wins over any
  // smoothing state.
  {
    TEST_CASE("explicit-snap");
    State state;
    state.Valid = true;
    state.X = 100.0f;
    state.Y = 100.0f;
    float out[2] = {};
    moduleui::follow::step(state, 300.0f, 300.0f, 400.0f, 34.0f, viewport(),
                           0.016f, true, out);
    CHECK_NEAR(out[0], 300.0f, kTolerance);
    CHECK_NEAR(out[1], 300.0f, kTolerance);
  }

  // reset() drops the sample so the next step snaps to the new target; this
  // is the "no stale position after a hide / map change" contract.
  {
    TEST_CASE("reset-snaps");
    State state;
    float first[2] = {};
    moduleui::follow::step(state, 900.0f, 900.0f, 400.0f, 34.0f, viewport(),
                           0.016f, false, first);
    moduleui::follow::reset(state);
    CHECK(!state.Valid);
    float second[2] = {};
    moduleui::follow::step(state, 120.0f, 120.0f, 400.0f, 34.0f, viewport(),
                           0.016f, false, second);
    CHECK_NEAR(second[0], 120.0f, kTolerance);
    CHECK_NEAR(second[1], 120.0f, kTolerance);
  }

  // A smoothed position is clamped on the way out: shrinking the viewport
  // must not leave the cluster outside the margins.
  {
    TEST_CASE("applied-position-clamped");
    State state;
    state.Valid = true;
    state.X = 1500.0f;
    state.Y = 900.0f;
    Limits small = viewport();
    small.Width = 640.0f;
    small.Height = 480.0f;
    float out[2] = {};
    moduleui::follow::step(state, 1500.0f, 900.0f, 400.0f, 34.0f, small, 0.016f,
                           false, out);
    CHECK(out[0] <= small.Width - small.Margin - 400.0f + kTolerance);
    CHECK(out[1] <= small.Height - small.Margin - 34.0f + kTolerance);
    CHECK(out[0] >= small.Margin - kTolerance);
    CHECK(out[1] >= small.Margin - kTolerance);
  }

  return abctest::finish("abc_ui_follow_anchor");
}
