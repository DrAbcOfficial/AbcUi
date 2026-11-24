// Contract test for the weapon sprite paths.
//
// svencoop_sdk cl_dll/ammo.cpp WeaponsResource::LoadWeaponSprites builds
// "sprites/<name>.txt", or "sprites/<dir>/<name>.txt" when the server sent a
// CustWeapon custom sprite directory, and "sprites/<atlas>.spr" per list
// entry. A later LoadSprites (WeaponSpr) only replaces the name, so the
// directory must stay part of every reload.
//
// The name and the directory are server strings. A map script leaves the
// custom directory with "../<dir>/<name>" (REPLACE_WEAPON_SPRITES, issue
// #141), so a ".." segment is refused only when folding it climbs above the
// game directory.
#include "data/WeaponSpritePath.h"

#include "framework/TestHarness.h"

#include <string>

int main() {
  using weaponres::SpritePathStatus;

  TEST_CASE("no custom directory");
  CHECK(weaponres::spriteListPath("weapon_crowbar", "") ==
        "sprites/weapon_crowbar.txt");

  TEST_CASE("custom sprite directory");
  CHECK(weaponres::spriteListPath("weapon_hl2_alyxgun_auto", "hl2") ==
        "sprites/hl2/weapon_hl2_alyxgun_auto.txt");

  TEST_CASE("trailing directory slash mirrors the shipped client");
  CHECK(weaponres::spriteListPath("weapon_ins2kukri", "ins2/mel/") ==
        "sprites/ins2/mel//weapon_ins2kukri.txt");

  TEST_CASE("a parent segment leaves the custom directory");
  CHECK(weaponres::spriteListPath("../blackops/weapon_bd_mp5", "bdsc_custom") ==
        "sprites/bdsc_custom/../blackops/weapon_bd_mp5.txt");
  CHECK(weaponres::checkSpriteListPath("../blackops/weapon_bd_mp5",
                                       "bdsc_custom") ==
        SpritePathStatus::kAccepted);
  CHECK(weaponres::checkSpriteListPath("blackops/weapon_crowbar", "") ==
        SpritePathStatus::kAccepted);

  TEST_CASE("a parent segment reaches, but never leaves, the game directory");
  // "sprites/../escape.txt" is still a file of the game directory.
  CHECK(weaponres::checkSpriteListPath("../escape", "") ==
        SpritePathStatus::kAccepted);
  CHECK(weaponres::checkSpriteListPath("../../escape", "") ==
        SpritePathStatus::kEscapesRoot);
  CHECK(weaponres::checkSpriteListPath("../../../escape", "bdsc_custom") ==
        SpritePathStatus::kEscapesRoot);
  CHECK(weaponres::checkSpriteListPath("weapon_bd_mp5", "../../../escape") ==
        SpritePathStatus::kEscapesRoot);

  TEST_CASE("the resource alphabet and limits still apply");
  CHECK(weaponres::checkSpriteListPath("weapon_bd;quit", "bdsc_custom") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath("../blackops/weapon bd_mp5",
                                       "bdsc_custom") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath("../..\\escape", "bdsc_custom") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath("", "bdsc_custom") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath("/blackops/weapon_bd_mp5", "") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath("weapon_bd_mp5", "/absolute") ==
        SpritePathStatus::kInvalidPart);
  CHECK(weaponres::checkSpriteListPath(std::string(128, 'a'), "") ==
        SpritePathStatus::kInvalidPart);

  TEST_CASE("the composed path keeps to the engine path budget");
  CHECK(weaponres::checkSpriteListPath(std::string(127, 'a'), "") ==
        SpritePathStatus::kAccepted);
  CHECK(weaponres::checkSpriteListPath(std::string(127, 'a'),
                                       std::string(127, 'b')) ==
        SpritePathStatus::kTooLong);

  TEST_CASE("the atlas path follows the same rule");
  CHECK(weaponres::spriteAtlasPath("blackops/640hud1") ==
        "sprites/blackops/640hud1.spr");
  CHECK(weaponres::checkSpriteAtlasPath("blackops/640hud1") ==
        SpritePathStatus::kAccepted);
  CHECK(weaponres::checkSpriteAtlasPath("../640hud1") ==
        SpritePathStatus::kAccepted);
  CHECK(weaponres::checkSpriteAtlasPath("../../640hud1") ==
        SpritePathStatus::kEscapesRoot);
  CHECK(weaponres::checkSpriteAtlasPath("") == SpritePathStatus::kInvalidPart);

  return abctest::finish("abc_ui_weapon_sprite_path");
}
