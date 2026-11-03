#include "WeaponSlotLayout.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <vector>

namespace weaponres {
namespace {

struct Target {
  int slot = 0;
  int pos = 0;
};

int clampSlot(int slot) {
  return std::clamp(slot, 0, kLayoutSlotCount - 1);
}

} // namespace

std::vector<LayoutResult> layoutWeapons(std::span<const LayoutInput> weapons) {
  std::vector<LayoutResult> results(weapons.size());
  std::vector<Target> targets(weapons.size());
  for (size_t i = 0; i < weapons.size(); ++i) {
    const LayoutInput &input = weapons[i];
    targets[i].slot =
        clampSlot(input.hasOverride ? input.overrideSlot : input.serverSlot);
    // Positions are unbounded ordering keys; they pass through as-is.
    targets[i].pos = input.hasOverride ? input.overridePos : input.serverPos;
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

  // Next-free-position scan per slot, unbounded upward (the old
  // WeaponList::Add `while (positions.count(pos)) pos++` shift).
  std::vector<std::set<int>> used(kLayoutSlotCount);
  for (const size_t index : order) {
    const Target target = targets[index];
    int pos = target.pos;
    while (used[target.slot].count(pos) > 0)
      ++pos;
    used[target.slot].insert(pos);
    results[index] = LayoutResult{target.slot, pos};
  }
  return results;
}

} // namespace weaponres
