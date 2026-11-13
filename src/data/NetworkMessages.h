#pragma once

// data::netmsg - UserMsg parse layer feeding the panel data model.
// Ported from ABCEnchance src/core/events/networkmessage.* (the batch-A
// message set; Damage/DeathMsg/MOTD/VoteMenu/... land with their batches).
// Parsing uses abc::MsgReader (09 §5: no second reader); a truncated message
// falls back to the original handler, matching the old CHECK_MSG_OK shape.
//
// Registration is HUD_Init-time only (plan 08 §4.9), through init() from the
// hook spine. Entry claims decide whether the native handler is skipped; the
// parse layer itself only publishes observation events and dispatch facts.

#include <string>
#include <vector>

#include <abc/Events.h>

namespace netmsg {

// WeaponList payload: the fields as they arrive on the wire (max ammo 255
// means unlimited, decoded to -1 like the old parser). Slot is the signed
// engine selector domain 0..9, position the unsigned byte domain 0..255 and
// id the positive short domain - none of them is clamped on this side.
struct WeaponListData {
  std::string name;
  int ammoType = -1;
  int max1 = -1;
  int ammo2Type = -1;
  int max2 = -1;
  int slot = -1;
  int slotPos = -1;
  int id = 0;
  int flags = 0;
};

// Damage UserMsg payload (old g_EventDamage tuple, value-semantic): armor and
// damage taken, the DMG_* tile bits for DmgTiles, and the world-space hit
// direction source for the Indicator.
struct DamageData {
  int armor = 0;
  int damage = 0;
  int tiles = 0;
  float from[3] = {};
};

// The old ABCCustomMsg/POPNUMBER MetaHook usermsg package (wire contract with
// the user's server-side addon - field order must not change): a world origin,
// a damage/heal value and an RGBA tint. Subtype POPNUMBER = 0, carrier type
// MHSV_CMD_ABC_CUSTOM = 77 (old networkmessage.h).
struct PopNumData {
  float origin[3] = {};
  int value = 0;
  int color[4] = {};
};

// VoiceMask: the server's voice state ack (issue 17). Two 32-slot bitmask
// words - who the server says is audible, and what it believes we banned -
// plus its own voice_modenable (absent = 0, the legacy read rule).
struct VoiceMaskData {
  unsigned long audible = 0;
  unsigned long serverBanned = 0;
  int modEnable = 0;
};

extern abc::CallbackList<void(int, int)> AmmoX; // (ammoIndex, count)
extern abc::CallbackList<void(const WeaponListData &)> WeaponList;
extern abc::CallbackList<void()> InitHud;
extern abc::CallbackList<void(int, const char *)> CustWeapon; // (id, sprName)
extern abc::CallbackList<void(int, int, int, int)>
    CurWeapon;                                  // (state, id, clip, clip2)
extern abc::CallbackList<void(int)> HideWeapon; // hide bits token
extern abc::CallbackList<void(int)> HideHud;    // hide bits token
extern abc::CallbackList<void(int, const char *)> WeaponSpr; // (id, sprName)
extern abc::CallbackList<void(int)> WeapPickup;              // weapon id
extern abc::CallbackList<void(int, int)> AmmoPickup;     // (ammoIndex, count)
extern abc::CallbackList<void(const char *)> ItemPickup; // hud.txt sprite name
extern abc::CallbackList<void(int)> Battery;             // armor
extern abc::CallbackList<void(int)> Health;
// (clientIndex 1..32, frags, deaths, health, armor, teamNumber, showExtras,
// isAdmin)
extern abc::CallbackList<void(int, float, int, float, float, int, int, int)>
    ScoreInfo;
extern abc::CallbackList<void(int)> FlashBat;         // flashlight battery
extern abc::CallbackList<void(bool, int)> Flashlight; // (on, battery)
extern abc::CallbackList<void(const DamageData &)> Damage;
extern abc::CallbackList<void(const PopNumData &)> PopNum;
// Batch C additions.
struct VoteMenuData {
  int type = 0; // old VoteType enum value (1 kill, 2 kick, 3 ban, 4 map, ...)
  std::string
      content; // server text (template with player/percent placeholders)
  std::string yes;
  std::string no;
};

// MapList: the server's vote-map catalog. clearAll == 0 starts a new list and
// carries the advertised total; a chunk carries [start,end) and one map token
// per entry. The old ABCEnchance hook appended chunks and kept the first
// clear as the new generation - the vote menu model preserves that contract.
struct MapListData {
  int clearAll = 0;
  int allCount = 0;
  int start = 0;
  int end = 0;
  std::vector<std::string> maps;
};

extern abc::CallbackList<void(int, bool)> Spectator; // (entindex, spectating)
extern abc::CallbackList<void(const char *)> ServerName;
extern abc::CallbackList<void(const char *)> NextMap;
extern abc::CallbackList<void(int)> TimeEnd;            // seconds
extern abc::CallbackList<void(int, const char *)> Motd; // (code, text chunk)
extern abc::CallbackList<void()> EndVote;
extern abc::CallbackList<void(const MapListData &)> MapList;
extern abc::CallbackList<void()> ClExtrasInfo;
// Issue 17: the voice state feed. ReqState means "send your vban/VModEnable
// now" (right after connect); VoiceMask is the server's ack of that state.
extern abc::CallbackList<void(const VoiceMaskData &)> VoiceMask;
extern abc::CallbackList<void()> ReqState;

// Inventory feed (issue 10). InvAdd carries one item exactly as the native
// CInventoryPanel::MsgFunc_InvAdd reads it: long id, three flag bytes, one
// float, then five NUL-terminated strings. The native window renders the
// second string as the item name (the first is its fallback) and the third
// as the description; the last two are the native selection key and an
// unused field, carried through so the wire contract is not narrowed.
struct InvItemData {
  int id = 0;
  bool canActivate = false;  // native item+0x04: Activate warning tint
  bool canDrop = false;      // native item+0x05: Drop warning tint
  bool keepOnClear = false; // native item+0x06: kept while preserve != 0
  float value = 0.0f;        // native item+0x08 (not rendered by the window)
  std::string name;          // native item+0x0c
  std::string displayName;   // native item+0x10 (preferred by the window)
  std::string description;   // native item+0x14
  std::string extra;         // native item+0x18 (unused by the window)
  std::string key;           // native item+0x1c (native selection key)
};
extern abc::CallbackList<void(const InvItemData &)> InvAdd;
// InvRemove: (item id, preserve). id 0 clears every item whose keepOnClear
// flag is not set; preserve != 0 keeps the keepOnClear items (the native
// CInventoryPanel::MsgFunc_InvRemove rule).
extern abc::CallbackList<void(int, int)> InvRemove;

// Parses one ShowMenu chunk, updates the framework menu fact, publishes the
// observation event and dispatches the netmsg.showmenu claim domain. Returns
// true when a claim handled it (the native handler must be skipped). The
// usermsg hook and the debug injector share this one entry.
bool dispatchShowMenu(int validSlots, int displaySeconds, int bits,
                      const char *text);

// HUD_Init: installs the HookUserMsg chain once per process. Repeated HUD
// cycles keep the existing chain (re-hooking every cycle would make two
// chained plugins point at each other).
void init();
// HUD_Init: re-chains the hooks after a client DLL reload rebuilt the usermsg
// table (detected through a client-image handler on a Ui-exclusive message).
// A normal map cycle leaves the table untouched and this is a no-op.
void refreshAfterClientLoad();
// The vote module hands its hud_vote cvar here at HUD_Init (console api) -
// the VoteMenu consumer passes messages through while the gate is off (old
// IsVoteEnable).
void setVoteGate(cvar_t *cvar);

} // namespace netmsg
