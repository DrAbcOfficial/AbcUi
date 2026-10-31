// abc_ui_segment_bar - the 10-point cell arithmetic of the abcui.division
// bars: the count follows the tracked maximum, the fixed bar length is split
// between the cells, and the last cell fills partially below a multiple of
// ten. Pure math, no RmlUi.
#include "common/SegmentBar.h"

#include <algorithm>
#include "framework/TestHarness.h"

using namespace moduleui::segment;

int main() {
  TEST_CASE("cell-count");
  CHECK(cellCount(100) == 10);
  CHECK(cellCount(150) == 15);
  CHECK(cellCount(80) == 8);
  CHECK(cellCount(45) == 5); // partial band still gets a cell
  CHECK(cellCount(1) == 1);
  CHECK(cellCount(0) == 1);
  CHECK(cellCount(-50) == 1);
  CHECK(cellCount(400) == 40);
  CHECK(cellCount(5000) == kMaxCells);

  TEST_CASE("cell-width");
  // 112dp bar, 1dp gaps: ten cells -> (112 - 9) / 10; fifteen -> (112 - 14)
  // / 15.
  CHECK_NEAR(cellWidth(112.0f, 1.0f, 10), 10.3, 0.001);
  CHECK_NEAR(cellWidth(112.0f, 1.0f, 15), 98.0f / 15.0f, 0.001);
  // One cell covers the whole length; degenerate counts cannot go negative.
  CHECK_NEAR(cellWidth(112.0f, 1.0f, 1), 112.0, 0.001);
  CHECK_NEAR(cellWidth(112.0f, 1.0f, 0), 112.0, 0.001);
  CHECK(cellWidth(4.0f, 1.0f, 40) >= 0.0f);

  TEST_CASE("fill-ratio");
  CHECK_NEAR(fillRatio(100, 9), 1.0, 0.001);
  CHECK_NEAR(fillRatio(95, 9), 0.5, 0.001); // partial last cell
  CHECK_NEAR(fillRatio(94, 9), 0.4, 0.001);
  CHECK_NEAR(fillRatio(90, 9), 0.0, 0.001); // empty last cell
  CHECK_NEAR(fillRatio(0, 0), 0.0, 0.001);
  CHECK_NEAR(fillRatio(150, 14), 1.0, 0.001);
  CHECK_NEAR(fillRatio(150, 15), 0.0, 0.001); // beyond the value
  CHECK_NEAR(fillRatio(-3, 0), 0.0, 0.001);

  TEST_CASE("fill-width");
  const float cell = cellWidth(112.0f, 1.0f, 10);
  CHECK_NEAR(fillWidth(95, 9, cell), cell * 0.5f, 0.001);
  CHECK_NEAR(fillWidth(100, 0, cell), cell, 0.001);

  return abctest::finish("abc_ui_segment_bar");
}
