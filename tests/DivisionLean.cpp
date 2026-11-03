// abc_ui_lean - the third-person lean solver behind the abcui.division HUD
// group. Pure math: the hip x rebuilt from the applied plate position (both
// sides) and the normalised lean amount with its clamp.
#include "common/Lean.h"

#include <cmath>
#include "framework/TestHarness.h"

namespace {

constexpr float kTolerance = 0.001f;

} // namespace

int main() {
  // Left side: the plate's right edge sits one gap left of the hip, so the
  // hip is the plate's left plus its width plus the gap (the inverse of the
  // follow target the module builds).
  {
    TEST_CASE("hip-left");
    const float hip = moduleui::lean::hipXFromApplied(700.0f, 150.0f, 24.0f,
                                                      false);
    CHECK_NEAR(hip, 700.0f + 150.0f + 24.0f, kTolerance);
  }

  // Right side: the plate's left edge sits one gap right of the hip.
  {
    TEST_CASE("hip-right");
    const float hip = moduleui::lean::hipXFromApplied(900.0f, 150.0f, 24.0f,
                                                      true);
    CHECK_NEAR(hip, 900.0f - 24.0f, kTolerance);
  }

  // The rebuilt hip is consistent with the follow target the module feeds the
  // solver: applying hipXFromApplied to the target on both sides returns the
  // hip it came from.
  {
    TEST_CASE("hip-roundtrip");
    const float hip = 800.0f;
    const float gap = 24.0f;
    const float clusterWidth = 150.0f;
    const float leftTarget = hip - clusterWidth - gap;
    const float rightTarget = hip + gap;
    CHECK_NEAR(
        moduleui::lean::hipXFromApplied(leftTarget, clusterWidth, gap, false),
        hip, kTolerance);
    CHECK_NEAR(
        moduleui::lean::hipXFromApplied(rightTarget, clusterWidth, gap, true),
        hip, kTolerance);
  }

  // A hip on the camera axis leans nothing; offsets lean linearly inside the
  // half width.
  {
    TEST_CASE("amount-linear");
    CHECK_NEAR(moduleui::lean::amount(960.0f, 1920.0f), 0.0f, kTolerance);
    CHECK_NEAR(moduleui::lean::amount(1440.0f, 1920.0f), 0.5f, kTolerance);
    CHECK_NEAR(moduleui::lean::amount(480.0f, 1920.0f), -0.5f, kTolerance);
  }

  // The amount clamps at the viewport edges and survives a degenerate
  // viewport (the caller guards width > 0, but the math must not divide).
  {
    TEST_CASE("amount-clamped");
    CHECK_NEAR(moduleui::lean::amount(5000.0f, 1920.0f), 1.0f, kTolerance);
    CHECK_NEAR(moduleui::lean::amount(-5000.0f, 1920.0f), -1.0f, kTolerance);
    CHECK_NEAR(moduleui::lean::amount(100.0f, 0.0f), 0.0f, kTolerance);
  }

  return abctest::finish("abc_ui_lean");
}
