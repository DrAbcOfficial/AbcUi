#pragma once

// data::weaponres - weapon registry, ammo counts and per-weapon sprite
// sources. Ported from ABCEnchance src/core/resource/weaponresource.*
// (value-semantic rebuild per plan 09 §5: one Weapon store keyed by id/name/
// slot-pos instead of three pointer lists over heap objects).
//
// Batch-A consumers: AmmoBar (current weapon ammo icons + counts), AmmoStack
// (ammo pickup icons), WeaponStack (pickup icons + HasAmmo tint). SelectSlot
// and the WeaponChoose feed stay with batch C. The ownership script events
// the old code fired here belong to AbcEssential signals (own diff) and are
// not duplicated.
//
// Sprite sources: sprites/<sprName>.txt parsed via SPR_GetList, atlas file +
// rect stored per slot; panels build spricon:// URIs through data::sprites.
// from: ABCEnchance/src/core/resource/weaponresource.*

#include <string>
#include <unordered_map>

#include <wrect.h>

struct weapon_data_s;

namespace weaponres {

constexpr size_t kMaxWeaponSlot = 10;
constexpr size_t kInvalidWeaponSlot = kMaxWeaponSlot + 1;

// Old CAmmoPanel::WEAPONSTATE - CurWeapon state bits the AmmoBar keys on.
enum WeaponState {
	kWeaponStateNotValid = 0,
	kWeaponStateValid = 1 << 0,
	kWeaponStateOnTarget = 1 << 1,
	kWeaponStateUzi = 1 << 2,
};

// Old ITEM_FLAGS (subset the registry logic uses).
enum ItemFlags {
	kItemFlagSelectOnEmpty = 1 << 0,
	kItemFlagExhaustible = 1 << 4,
};

struct Weapon {
	std::string name;
	std::string sprName;
	int ammoType = -1;
	int ammo2Type = -1;
	int max1 = -1;
	int max2 = -1;
	int slot = 0;
	int slotPos = 0;
	int flags = 0;
	int id = 0;
	int clip = 0;
	int clip2 = 0;
	int state = 0;

	// Sprite source (sprites/<atlas> crop). invalid = the entry was missing
	// from the weapon's sprite list.
	struct SpriteSource {
		std::string atlas;
		wrect_t rect{};
		bool valid = false;
	};
	SpriteSource active;         // "weapon_s"
	SpriteSource inactive;       // "weapon"
	SpriteSource ammo;           // "ammo"
	SpriteSource ammo2;          // "ammo2" (falls back to "ammo")
	SpriteSource crosshair;      // batch B
	SpriteSource autoaim;        // batch B
	SpriteSource zoomedCrosshair;
	SpriteSource zoomedAutoaim;
};

// HUD_Init-time: subscribes the netmsg weapon feed. Idempotent.
void init();
// Map transition (HUD_VidInit chain): registry starts from scratch.
void reset();
// Prediction sync (HUD_TxferPredictionData chain): ownership + clip diff.
void onTxferPredictionData(const weapon_data_s* weaponDataArray);

const Weapon* getWeapon(int id);
const Weapon* getWeapon(const char* name);
const Weapon* getWeapon(int slot, int pos);
bool hasWeapon(int slot, int pos);
const Weapon* currentWeapon();

int countAmmo(int ammoType);
// Old HasAmmo: unlimited weapons (max1/max2 == -1) always true.
bool hasAmmo(const Weapon* weapon);
// Old GetAmmoPicFromWeapon: icon source of the ammo type, from any weapon
// using it. Returns false when no weapon carries the type.
bool ammoIcon(int ammoType, std::string& atlas, wrect_t& rect);

}
