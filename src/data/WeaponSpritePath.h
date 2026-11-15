#pragma once

// The sprite-list file a weapon's HUD sprites are read from (svencoop_sdk
// cl_dll/ammo.cpp WeaponsResource::LoadWeaponSprites):
//   sprites/<spriteName>.txt
//   sprites/<customDirectory>/<spriteName>.txt  (after a CustWeapon message)
// A trailing slash in the directory produces the same doubled separator the
// shipped client produces; the engine normalizes it, so the bytes must match.
#include <string>
#include <string_view>

namespace weaponres {

inline std::string spriteListPath(std::string_view spriteName,
                                  std::string_view spriteDirectory) {
  std::string path = "sprites/";
  if (!spriteDirectory.empty()) {
    path += spriteDirectory;
    path += '/';
  }
  path += spriteName;
  path += ".txt";
  return path;
}

} // namespace weaponres
