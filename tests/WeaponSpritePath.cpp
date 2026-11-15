// Contract test for the weapon sprite-list path.
//
// svencoop_sdk cl_dll/ammo.cpp WeaponsResource::LoadWeaponSprites builds
// "sprites/<name>.txt", or "sprites/<dir>/<name>.txt" when the server sent a
// CustWeapon custom sprite directory. A later LoadSprites (WeaponSpr) only
// replaces the name, so the directory must stay part of every reload.
#include "data/WeaponSpritePath.h"

#include "framework/TestHarness.h"

int main() {
  TEST_CASE("no custom directory");
  CHECK(weaponres::spriteListPath("weapon_crowbar", "") ==
        "sprites/weapon_crowbar.txt");

  TEST_CASE("custom sprite directory");
  CHECK(weaponres::spriteListPath("weapon_hl2_alyxgun_auto", "hl2") ==
        "sprites/hl2/weapon_hl2_alyxgun_auto.txt");

  TEST_CASE("trailing directory slash mirrors the shipped client");
  CHECK(weaponres::spriteListPath("weapon_ins2kukri", "ins2/mel/") ==
        "sprites/ins2/mel//weapon_ins2kukri.txt");

  return abctest::finish("abc_ui_weapon_sprite_path");
}
