#pragma once

// Engine prediction weapon domain.
//
// svencoop_sdk/common/entity_state.h: local_state_t::weapondata[256] and
// cl_dll/hl/hl_weapons.h: MAX_WEAPONS 256. The array is indexed by the full
// server weapon id; registering an id outside the array makes the prediction
// diff read past the end. Server data is untrusted, so the domain check is
// the registration boundary.
namespace weapondomain {

inline constexpr int kPredictionWeaponCount = 256;

inline bool validWeaponId(int id) {
  return id > 0 && id < kPredictionWeaponCount;
}

} // namespace weapondomain
