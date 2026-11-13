#pragma once

// Pure field decoders for the client UserMsg wire quirks the SDK spells out.
// Kept out of the hook bodies so the offline suite can pin them without an
// engine or usermsg table.
#include <climits>

#include <abc/NetMsg.h>

namespace netmsg {

// svencoop_sdk cl_dll/ammo.cpp (CHudAmmo::MsgFunc_HideHUD) reads the hide-bit
// token as a short: bits 8/9 are HIDEHUD_CROSSHAIR and HIDEHUD_AMMO.
inline bool decodeHideHud(const void *buffer, int size, int &token) {
  abc::MsgReader msg(buffer, size);
  token = msg.readShort();
  return msg.ok();
}

// svencoop_sdk cl_dll/ammo.cpp (CHudAmmo::MsgFunc_AmmoX) stores
// abs(iCount); a negative count means the same magnitude.
inline bool normalizeAmmoCount(int raw, int &count) {
  const long long magnitude =
      raw < 0 ? -static_cast<long long>(raw) : static_cast<long long>(raw);
  if (magnitude > INT_MAX)
    return false;
  count = static_cast<int>(magnitude);
  return true;
}

// svencoop_sdk cl_dll/vgui_VotePanel.h: MAPLIST_END is 123, distinct from the
// MAPLIST_RESET (0) payload.
inline constexpr int kMapListEnd = 123;

} // namespace netmsg
