#include "common/OpacityMotion.h"
#include "framework/TestHarness.h"

#include <limits>

int main() {
  using moduleui::opacity::Ramp;
  float value = -1.0f;
  TEST_CASE("linear-and-settled");
  Ramp fade;
  fade.retarget(1.0f, 0.2);
  CHECK(fade.advance(0.05, value));
  CHECK_NEAR(value, 0.25, 0.001);
  fade.retarget(1.0f, 0.2); // a repeated target must not restart
  CHECK(fade.advance(0.05, value));
  CHECK_NEAR(value, 0.5, 0.001);
  CHECK(fade.advance(0.1, value));
  CHECK_NEAR(value, 1.0, 0.001);
  CHECK(!fade.advance(0.1, value));

  TEST_CASE("reverse-from-current");
  fade.retarget(0.0f, 0.2);
  CHECK(fade.advance(0.1, value));
  CHECK_NEAR(value, 0.5, 0.001);
  fade.retarget(1.0f, 0.2);
  CHECK(fade.advance(0.1, value));
  CHECK_NEAR(value, 0.75, 0.001);

  TEST_CASE("invalid-time-pauses");
  fade.advance(0.0, value); // consume a pending target write
  for (double delta : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(), -1.0}) {
    CHECK(!fade.advance(delta, value));
    CHECK_NEAR(value, 0.75, 0.001);
  }
  CHECK(fade.advance(1000.0, value));
  CHECK_NEAR(value, 1.0, 0.001);

  TEST_CASE("reset-and-format");
  fade.reset();
  CHECK(fade.advance(0.0, value));
  CHECK_NEAR(value, 0.0, 0.0);
  char text[16];
  moduleui::opacity::text(value, text);
  CHECK(text[0] == '0');
  fade.retarget(1.0f, 0.0);
  CHECK(fade.advance(0.0, value));
  CHECK_NEAR(value, 1.0, 0.0);
  return abctest::finish("abc_ui_opacity_motion");
}
