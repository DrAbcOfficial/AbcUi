#pragma once

// moduleui::segment - the 10-point cell arithmetic behind the segmented
// health and armour bars (abcui.division).
//
// Pure math on dp/points so the two rules that are easy to get wrong stay
// testable without RmlUi: the cell count follows the tracked maximum (a
// 150 HP or an 80 HP maximum still fills the bar with 10-point cells) and the
// last cell only fills partially when the value is not a multiple of ten.

#include <algorithm>

namespace moduleui::segment {

inline constexpr int kPointsPerCell = 10;
// Upper bound the bar DOM is built for (400 points).
inline constexpr int kMaxCells = 40;

// One cell per 10 points of the maximum, clamped to the buildable range.
inline int cellCount(int maximum) {
  const int clamped = std::clamp(maximum, 1, kMaxCells * kPointsPerCell);
  const int cells = (clamped + kPointsPerCell - 1) / kPointsPerCell;
  return std::clamp(cells, 1, kMaxCells);
}

// Shared width of one cell: the bar length is fixed, the count varies.
inline float cellWidth(float total, float gap, int count) {
  if (count <= 0)
    return total;
  return std::max(0.0f,
                  (total - (count - 1) * gap) / static_cast<float>(count));
}

// Fraction (0..1) of cell `index` covered by `value`.
inline float fillRatio(int value, int index) {
  const float within =
      static_cast<float>(value) - index * static_cast<float>(kPointsPerCell);
  return std::clamp(within / static_cast<float>(kPointsPerCell), 0.0f, 1.0f);
}

inline float fillWidth(int value, int index, float cellWidth) {
  return fillRatio(value, index) * cellWidth;
}

} // namespace moduleui::segment
