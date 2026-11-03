#pragma once

// data::WeaponSlotLayout - the pure client-side selector layout.
//
// The registry owns the weapon records; this unit only maps the raw server
// placement plus the client override targets to the positions the selector
// shows. Keeping it engine-free lets the offline suite pin the ordering and
// collision rules.

#include <span>
#include <string_view>
#include <vector>

namespace weaponres {

// Selector slot domain (the old MAX_WEAPON_SLOT).
inline constexpr int kLayoutSlotCount = 10;

// One weapon's placement input. hasOverride means "a rule exists and the
// client filter is enabled"; the raw server values are always supplied so a
// disabled filter reproduces the server layout unchanged.
struct LayoutInput {
  std::string_view name;
  int serverSlot = 0;
  int serverPos = 0;
  bool hasOverride = false;
  int overrideSlot = 0;
  int overridePos = 0;
};

struct LayoutResult {
  int slot = 0;
  int pos = 0;
};

// Assigns every weapon an effective (slot, position), returned in input
// order:
//
// 1. The target is the override when present, the server placement otherwise;
//    the slot is clamped to 0..9. Positions are pure ordering keys and pass
//    through unclamped - the server byte domain, the rule grammar and the
//    shifted results all stay addressable.
// 2. Weapons are placed by (slot, position), overrides before server entries
//    at the same target, then by name, then by input order, so the result does
//    not depend on registration order.
// 3. A taken target moves the later weapon to the next free position inside
//    the same slot, without bound (the old WeaponList::Add shift). DO NOT
//    clamp, cap or pin the position here: a ceiling stacks every overflow
//    weapon on one unaddressable key and the selector loses them.
std::vector<LayoutResult> layoutWeapons(std::span<const LayoutInput> weapons);

} // namespace weaponres
