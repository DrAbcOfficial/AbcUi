#include "WeaponSlotLayout.h"

#include <algorithm>
#include <array>
#include <numeric>

namespace weaponres {
namespace {

struct Target {
  int slot = 0;
  int pos = 0;
};

int clampSlot(int slot) {
  return std::clamp(slot, 0, kLayoutSlotCount - 1);
}

int clampPosition(int position) {
  return std::clamp(position, 0, kLayoutMaxPosition);
}

} // namespace

std::vector<LayoutResult> layoutWeapons(std::span<const LayoutInput> weapons) {
  std::vector<LayoutResult> results(weapons.size());
  std::vector<Target> targets(weapons.size());
  for (size_t i = 0; i < weapons.size(); ++i) {
    const LayoutInput &input = weapons[i];
    targets[i].slot =
        clampSlot(input.hasOverride ? input.overrideSlot : input.serverSlot);
    targets[i].pos =
        clampPosition(input.hasOverride ? input.overridePos : input.serverPos);
  }

  std::vector<size_t> order(weapons.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Target &left = targets[a];
    const Target &right = targets[b];
    if (left.slot != right.slot)
      return left.slot < right.slot;
    if (left.pos != right.pos)
      return left.pos < right.pos;
    // Overrides claim their target before a server entry already sitting
    // there; the name then keeps equal-target rules order-independent.
    if (weapons[a].hasOverride != weapons[b].hasOverride)
      return weapons[a].hasOverride;
    if (weapons[a].name != weapons[b].name)
      return weapons[a].name < weapons[b].name;
    return a < b;
  });

  std::array<std::array<bool, kLayoutMaxPosition + 1>, kLayoutSlotCount> used{};
  for (const size_t index : order) {
    const Target target = targets[index];
    int pos = target.pos;
    while (pos <= kLayoutMaxPosition && used[target.slot][pos])
      ++pos;
    if (pos > kLayoutMaxPosition)
      pos = kLayoutMaxPosition;
    used[target.slot][pos] = true;
    results[index] = LayoutResult{target.slot, pos};
  }
  return results;
}

} // namespace weaponres
