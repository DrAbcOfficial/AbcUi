// abc_ui_value_bounce - the shared number pop behind the HUD readouts: the
// peak frame, the eased return to rest, the restart rules and the inline
// transform text. Pure math, no RmlUi.
#include "common/ValueBounce.h"

#include <string>

#include "framework/TestHarness.h"

using moduleui::bounce::Pulse;

int main() {
  TEST_CASE("idle-writes-nothing");
  {
    Pulse pulse;
    float scale = 0.5f;
    CHECK(!pulse.active());
    // An idle pulse must neither report a frame nor touch the caller's value.
    CHECK(!pulse.advance(0.016, scale));
    CHECK(scale == 0.5f);
  }

  TEST_CASE("change-bursts-on-the-first-frame");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.active());
    // The first advance reports the peak without consuming time, so the burst
    // lands on the frame the value changed.
    CHECK(pulse.advance(0.016, scale));
    CHECK_NEAR(scale, moduleui::bounce::kPeakScale, 1e-6);
  }

  TEST_CASE("eases-back-to-rest");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.advance(0.016, scale));
    const float peak = scale;
    float previous = scale;
    bool sawMiddle = false;
    for (int frame = 0; frame < 64 && pulse.active(); ++frame) {
      CHECK(pulse.advance(1.0 / 60.0, scale));
      // The return is an ease-out: it only shrinks, and never below rest.
      CHECK(scale <= previous + 1e-6f);
      CHECK(scale >= 1.0f);
      if (scale > 1.0f && scale < peak)
        sawMiddle = true;
      previous = scale;
    }
    CHECK(!pulse.active());
    CHECK(sawMiddle);
    CHECK_NEAR(previous, 1.0, 1e-6);
  }

  TEST_CASE("a-long-frame-finishes");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.advance(0.0, scale));
    CHECK(pulse.advance(moduleui::bounce::kDuration * 2.0, scale));
    CHECK_NEAR(scale, 1.0, 1e-6);
    CHECK(!pulse.active());
    // Once over it stays over until the next trigger.
    CHECK(!pulse.advance(0.016, scale));
  }

  TEST_CASE("zero-frame-holds");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.advance(0.0, scale));
    const float peak = scale;
    // A zero or backwards clock sample is ignored, not accumulated.
    CHECK(pulse.advance(0.0, scale));
    CHECK_NEAR(scale, peak, 1e-6);
    CHECK(pulse.advance(-0.5, scale));
    CHECK_NEAR(scale, peak, 1e-6);
  }

  TEST_CASE("a-second-change-restarts-from-the-peak");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.advance(0.016, scale));
    CHECK(pulse.advance(0.05, scale));
    CHECK(scale < moduleui::bounce::kPeakScale);
    // A change inside the pop starts a fresh burst instead of queueing one.
    pulse.trigger();
    CHECK(pulse.advance(0.016, scale));
    CHECK_NEAR(scale, moduleui::bounce::kPeakScale, 1e-6);
  }

  TEST_CASE("reset-drops-the-pop");
  {
    Pulse pulse;
    float scale = 0.0f;
    pulse.trigger();
    CHECK(pulse.advance(0.016, scale));
    pulse.reset();
    CHECK(!pulse.active());
    CHECK(!pulse.advance(0.016, scale));
  }

  TEST_CASE("transform-text");
  char buffer[32] = {};
  // A settled factor drops the inline transform instead of writing scale(1).
  CHECK(std::string(moduleui::bounce::transformText(
            1.0f, buffer, sizeof(buffer))) == moduleui::bounce::kRestTransform);
  CHECK(std::string(moduleui::bounce::transformText(
            1.001f, buffer, sizeof(buffer))) == "scale(1.001)");
  CHECK(std::string(moduleui::bounce::transformText(
            moduleui::bounce::kPeakScale, buffer, sizeof(buffer))) ==
        "scale(1.280)");

  return abctest::finish("abc_ui_value_bounce");
}
