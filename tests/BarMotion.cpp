// abc_ui_bar_motion - the shared smooth-bar policy behind the HUD's health,
// armour and battery fills: the 1% width ladder, the direction/snap class
// tokens and the write tracker (a repeated value never restarts a running
// transition, a hidden bar snaps back). Pure math, no RmlUi.
#include "common/BarMotion.h"

#include <cmath>
#include <limits>
#include <string>

#include "framework/TestHarness.h"

using moduleui::barmotion::Direction;
using moduleui::barmotion::Step;
using moduleui::barmotion::Tracker;

namespace {

bool same(const Step &step, bool write, bool snap, Direction direction) {
  return step.write == write && step.snap == snap &&
         step.direction == direction;
}

} // namespace

int main() {
  TEST_CASE("percent");
  CHECK(moduleui::barmotion::percent(0.0f) == 0);
  CHECK(moduleui::barmotion::percent(1.0f) == 100);
  CHECK(moduleui::barmotion::percent(0.5f) == 50);
  CHECK(moduleui::barmotion::percent(0.95f) == 95);
  // Rounded to the nearest whole class.
  CHECK(moduleui::barmotion::percent(0.954f) == 95);
  CHECK(moduleui::barmotion::percent(0.955f) == 96);
  // Negative, over-full and non-finite fractions clamp.
  CHECK(moduleui::barmotion::percent(-0.5f) == 0);
  CHECK(moduleui::barmotion::percent(3.0f) == 100);
  CHECK(moduleui::barmotion::percent(std::numeric_limits<float>::quiet_NaN()) ==
        0);

  TEST_CASE("width-class");
  char buffer[moduleui::barmotion::kClassCapacity] = {};
  CHECK(std::string(moduleui::barmotion::widthClass(
            73, buffer, sizeof(buffer))) == "bar-fill-73");
  CHECK(std::string(moduleui::barmotion::widthClass(
            150, buffer, sizeof(buffer))) == "bar-fill-100");
  CHECK(std::string(moduleui::barmotion::widthClass(
            -4, buffer, sizeof(buffer))) == "bar-fill-0");

  TEST_CASE("motion-classes");
  CHECK(std::string(moduleui::barmotion::motionClasses(
            Direction::kDrop, false, buffer, sizeof(buffer))) ==
        "bar-motion bar-drop");
  CHECK(std::string(moduleui::barmotion::motionClasses(
            Direction::kGain, false, buffer, sizeof(buffer))) ==
        "bar-motion bar-gain");
  // A snap wins over a direction (it is the only instant mode).
  CHECK(std::string(moduleui::barmotion::motionClasses(
            Direction::kDrop, true, buffer, sizeof(buffer))) ==
        "bar-motion bar-snap");

  TEST_CASE("fill-classes");
  CHECK(std::string(moduleui::barmotion::fillClasses(
            "fill", 50, buffer, sizeof(buffer))) == "fill bar-fill-50");
  CHECK(std::string(moduleui::barmotion::fillClasses(
            "", 100, buffer, sizeof(buffer))) == "bar-fill-100");

  TEST_CASE("bar-classes");
  // The classic vitals meter is box and fill in one element.
  CHECK(std::string(moduleui::barmotion::barClasses(Direction::kGain, false, 95,
                                                    buffer, sizeof(buffer))) ==
        "bar-motion bar-gain bar-fill-95");
  CHECK(std::string(moduleui::barmotion::barClasses(Direction::kNone, true, 0,
                                                    buffer, sizeof(buffer))) ==
        "bar-motion bar-snap bar-fill-0");

  TEST_CASE("tracker-first-show-snaps");
  {
    Tracker tracker;
    CHECK(same(tracker.observe(100, true), true, true, Direction::kNone));
    // A repeated readout writes nothing, so a transition in flight survives.
    CHECK(same(tracker.observe(100, true), false, false, Direction::kNone));
  }

  TEST_CASE("tracker-directions");
  {
    Tracker tracker;
    CHECK(same(tracker.observe(100, true), true, true, Direction::kNone));
    CHECK(same(tracker.observe(95, true), true, false, Direction::kDrop));
    CHECK(same(tracker.observe(97, true), true, false, Direction::kGain));
    // Same value again: no write, no restart.
    CHECK(same(tracker.observe(97, true), false, false, Direction::kNone));
  }

  TEST_CASE("tracker-hide-resets");
  {
    Tracker tracker;
    CHECK(same(tracker.observe(100, true), true, true, Direction::kNone));
    CHECK(same(tracker.observe(60, false), false, false, Direction::kNone));
    // The bar was not drawn while the value moved: show the current one
    // without animating from the last drawn width.
    CHECK(same(tracker.observe(60, true), true, true, Direction::kNone));
  }

  TEST_CASE("tracker-reset");
  {
    Tracker tracker;
    CHECK(same(tracker.observe(80, true), true, true, Direction::kNone));
    tracker.reset();
    CHECK(same(tracker.observe(80, true), true, true, Direction::kNone));
  }

  TEST_CASE("tracker-zero");
  {
    // Zero is a real value, not the unset marker: its first write snaps, and
    // a repeated zero does not.
    Tracker tracker;
    CHECK(same(tracker.observe(0, true), true, true, Direction::kNone));
    CHECK(same(tracker.observe(0, true), false, false, Direction::kNone));
    CHECK(same(tracker.observe(1, true), true, false, Direction::kGain));
    CHECK(same(tracker.observe(0, true), true, false, Direction::kDrop));
  }

  return abctest::finish("abc_ui_bar_motion");
}
