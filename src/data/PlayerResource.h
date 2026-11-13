#pragma once

// data::playerres - the 32-slot player info store (ScoreInfo wire values +
// engine studio PlayerInfo mirror). from: ABCEnchance/src/core/resource/
// playerresource.*.
//
// Backend notes (user directive: the steamapi layer is swappable - see
// rmlui/SteamAPI.h): the SteamID64 column comes from the engine's
// player_info_sc_t::m_nSteamID for every slot, with the local slot falling
// back to steamapi::localSteamId(). The persona-name column rides
// steamapi::personaName() (old GetFriendPersonaName) - empty while the
// active backend cannot serve names.
//
// Donor rides the client dll's native HUD panel info table (sig-located
// by the spine, see hooks.h) exactly like the old g_aryNativeHUDPanelInfo.
// The wire carries only the "show extras" bit (ScoreInfo byte 6); the level
// itself arrives through the encrypted ClExtrasInfo message and is cached by
// the client dll at g_PlayerExtraInfo[index] + 0x54. Admin comes off the
// ScoreInfo wire byte.

#include <string>

#include "TeamResource.h"

#include <cstdint>

struct player_info_s;

namespace playerres {

constexpr int kMaxPlayers = 32;

// Old PlayerInfo::ADMIN.
enum class Admin : int {
  None = 0,
  Operator,
  ServerOwner,
};

// Old PlayerInfo::DONOR - the native word at g_PlayerExtraInfo[index] + 0x54
// packs "showextras << 16 | extraslevel"; decodeDonor folds it into the old
// 0x10000 + kind form.
enum class Donor : int {
  None = 65536,
  ElectricCrowbar,
  GoldedUzi,
  GoldedDollar,
  Tester,
  Artist,
  Developer,
};

constexpr int32_t kDonorShownBit = 0x10000;
constexpr int32_t kDonorLevelMask = 0xFFFF;

// The client dll's ClExtrasInfo handler writes the level into the low word
// and its ScoreInfo handler writes the showextras flag into bit 16. Hidden
// badges and unknown levels decode to None, so the caller never has to look
// at the ScoreInfo wire byte (which lags the native write by one message).
inline Donor decodeDonor(int32_t raw) {
  const int level = raw & kDonorLevelMask;
  return (raw & kDonorShownBit) != 0 && level >= 1 && level <= 6
             ? static_cast<Donor>(kDonorShownBit | level)
             : Donor::None;
}

struct PlayerInfo {
  int index = 0; // 1..32 (entindex)
  int ping = 0;
  int loss = 0;
  std::string model;
  std::string name;
  std::string realName;
  int topColor = 0;
  int bottomColor = 0;
  float frags = 0.0f;
  int deaths = 0;
  float health = 0.0f;
  float armor = 0.0f;
  teamres::TeamId team = teamres::TeamId::None;
  Admin admin = Admin::None;
  Donor donor = Donor::None;
  bool connected = false;
  bool spectate = false;
  std::uint64_t steamId64 = 0;
#ifndef NDEBUG
  // abc_ui_board instrument: the slot identity was injected by the Debug
  // acceptance driver - update() keeps it instead of the empty engine
  // mirror (a local game has no real players in slots 2..32).
  bool debugPinned = false;
#endif

  // Old IsValid: connected AND named.
  bool isValid() const {
    return connected && !name.empty();
  }
  bool isThisPlayer() const;
  // Re-reads the engine mirror (studio PlayerInfo) + native donor.
  void update();
  // Issue #9: the periodic scoreboard path. Reads ONLY ping/packet_loss from
  // the same engine mirror - the full update() may reset identity/SteamID
  // state, which a 0.5 s refresh must never do. Null mirror / invalid slot
  // keep the last known values; a negative engine value clamps to 0. The
  // mirror carries non-zero pings only while the client reports the score
  // state (the score seat's usercmd IN_SCORE).
  void refreshPingLoss();
  void reset();
};

// HUD_Init-time: subscribes the ScoreInfo/Spectator/ClExtras feed. Idempotent.
void init();
// Map transition (HUD_VidInit chain).
void resetAll();
// ClExtras / periodic refresh: re-reads every engine mirror.
void updateAll();
// Issue #9: the cheap periodic variant of updateAll() - every valid slot's
// ping/packet_loss only (identity, SteamID, avatar and donor state must
// survive a refresh). Used by the scoreboard's 0.5 s rebuild.
void refreshPingLossAll();

// idx is the entindex 0..32 (0/null -> nullptr), old GetPlayerInfo.
PlayerInfo *getPlayerInfo(int idx);
PlayerInfo *getLocalPlayerInfo();

// Old PlayerResource::IsInSpectate: the entity's iuser1 observer flag.
bool isEntitySpectate(int idx);

#ifndef NDEBUG
// abc_ui_board instrument: fill slots 2..32 with fake identities (mixed
// teams/frags/health) so a local session can drive the scoreboard/table
// interactions at full occupancy. Debug builds only.
void debugPopulateBoard();
#endif

} // namespace playerres
