// Offline acceptance for issue #9: the scoreboard's ping/loss must
// follow the live engine mirror on the 0.5 s rebuild, not only on
// ScoreInfo/Spectator/ClExtras events.
//
// Links the production PlayerResource store (and the NetworkMessages callback
// lists it subscribes to) with a fake engine studio mirror. Pins the
// refresh contract: ping/loss only, clamped, null-mirror safe, invalid slots
// refused, and identity/SteamID/model/donor/admin untouched - the reason the
// full update() may not be reused for the periodic path.
//
// The other half of issue #9 lives in the scoreboard/CL_CreateMove layer:
// the server streams ping/loss only to clients reporting usercmd IN_SCORE
// (the old Viewport m_bInScore), and without it the engine tables sit at
// 0/0 for the whole session. The store layer stays local-agnostic.
//

#include <metahook.h>

#include <com_model.h>
#include <cvardef.h>
#include <r_studioint.h>

#include "data/NetworkMessages.h"
#include "data/PlayerResource.h"
#include "hooks.h"
#include "services/steam/SteamAPI.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include "framework/TestHarness.h"

// --- engine / plugin seams --------------------------------------------------

cl_enginefunc_t gEngfuncs{};
metahook_api_t MetaApi{};
metahook_api_t *g_pMetaHookAPI = &MetaApi;
engine_studio_api_t StudioApi{};

// Core globals normally come from CoreGlobals.cpp; this executable defines
// them itself so the module links without the full framework host.
namespace abc {
PVOID g_dwClientBase = nullptr;
DWORD g_dwClientSize = 0;
} // namespace abc

namespace framework::module_resources {
void reportException(const char *, const char *) {}
} // namespace framework::module_resources

namespace hooks {
engine_studio_api_t *EngineStudioApi = &StudioApi;
NativePlayerInfoView NativeHudPanelInfo{};
GetClientColorFn GetClientColor = nullptr;
} // namespace hooks

namespace steamapi {
SteamId64 localSteamId() {
  return 0;
}
std::string personaName(SteamId64) {
  return {};
}
} // namespace steamapi

namespace {

// --- the fake studio mirror -------------------------------------------------
// `Mirror[slot]` is the engine's player table entry (PlayerInfo(slot - 1));
// MirrorPresent[slot] = false models a slot the mirror does not carry.

cl_entity_t LocalEntity{};
cl_entity_t Entities[33]{};
player_info_sc_t Mirror[33]{};
bool MirrorPresent[33] = {};

cl_entity_t *FakeLocalPlayer() {
  return &LocalEntity;
}

cl_entity_t *FakeEntityByIndex(int index) {
  return index >= 1 && index <= 32 ? &Entities[index] : nullptr;
}

player_info_s *FakePlayerInfo(int slot) {
  if (slot < 0 || slot > 31 || !MirrorPresent[slot + 1])
    return nullptr;
  return &Mirror[slot + 1];
}

void putMirror(int slot, const char *name, unsigned long long steamId, int ping,
               int loss) {
  MirrorPresent[slot] = true;
  std::snprintf(Mirror[slot].name, sizeof(Mirror[slot].name), "%s", name);
  std::snprintf(Mirror[slot].model, sizeof(Mirror[slot].model), "%s", "gordon");
  Mirror[slot].m_nSteamID = steamId;
  Mirror[slot].ping = ping;
  Mirror[slot].packet_loss = loss;
  Mirror[slot].topcolor = 1;
  Mirror[slot].bottomcolor = 2;
}

} // namespace

int main() {
  gEngfuncs.GetLocalPlayer = FakeLocalPlayer;
  gEngfuncs.GetEntityByIndex = FakeEntityByIndex;
  StudioApi.PlayerInfo = FakePlayerInfo;

  // Slot 2 joins; the store learns its identity from the engine mirror.
  putMirror(2, "Mate", 76561197960287930ull, 12, 0);
  playerres::updateAll();
  playerres::PlayerInfo *mate = playerres::getPlayerInfo(2);
  CHECK(mate && mate->isValid());

  TEST_CASE("refresh reads the live ping/loss");
  {
    // The ScoreInfo wire carries no ping: a slot that only ever saw one keeps
    // its zero until the periodic refresh reads the mirror (issue #9).
    mate->ping = 0;
    mate->loss = 0;
    Mirror[2].ping = 155;
    Mirror[2].packet_loss = 7;
    playerres::refreshPingLossAll();
    CHECK(mate->ping == 155);
    CHECK(mate->loss == 7);
  }

  TEST_CASE("refresh never touches identity or flags");
  {
    mate->steamId64 = 0x1234;
    mate->name = "Mate";
    mate->model = "gordon";
    mate->frags = 31.0f;
    mate->health = 88.0f;
    mate->armor = 42.0f;
    mate->team = teamres::TeamId::Team1;
    mate->admin = playerres::Admin::Operator;
    mate->donor = playerres::Donor::Tester;
    // The engine slot now belongs to someone else - exactly the state the
    // full update() reacts to by resetting the record.
    std::snprintf(Mirror[2].name, sizeof(Mirror[2].name), "%s", "Intruder");
    Mirror[2].m_nSteamID = 0x9999;
    Mirror[2].model[0] = 'x';
    Mirror[2].topcolor = 9;
    Mirror[2].ping = -4; // engine garbage: clamp, never keep a negative
    Mirror[2].packet_loss = -9;
    mate->refreshPingLoss();
    CHECK(mate->ping == 0);
    CHECK(mate->loss == 0);
    CHECK(mate->steamId64 == 0x1234);
    CHECK(mate->name == "Mate");
    CHECK(mate->model == "gordon");
    CHECK(mate->frags == 31.0f);
    CHECK(mate->health == 88.0f);
    CHECK(mate->armor == 42.0f);
    CHECK(mate->team == teamres::TeamId::Team1);
    CHECK(mate->admin == playerres::Admin::Operator);
    CHECK(mate->donor == playerres::Donor::Tester);
  }

  TEST_CASE("a missing mirror entry keeps the last known values");
  {
    mate->ping = 64;
    mate->loss = 3;
    MirrorPresent[2] = false;
    mate->refreshPingLoss();
    CHECK(mate->ping == 64);
    CHECK(mate->loss == 3);
    MirrorPresent[2] = true;
  }

  TEST_CASE("invalid slots are refused");
  {
    playerres::PlayerInfo beyond;
    beyond.index = playerres::kMaxPlayers + 1;
    beyond.ping = 42;
    beyond.loss = 5;
    beyond.refreshPingLoss();
    CHECK(beyond.ping == 42);
    CHECK(beyond.loss == 5);

    playerres::PlayerInfo zero;
    zero.index = 0;
    zero.ping = 8;
    zero.loss = 2;
    zero.refreshPingLoss();
    CHECK(zero.ping == 8);
    CHECK(zero.loss == 2);
  }

  TEST_CASE("the bulk refresh skips rows the board would not render");
  {
    // Slot 4 has a live mirror but no valid store row (never identified):
    // its stale value must survive - only rendered rows are refreshed.
    putMirror(4, "Ghost", 76561197960287931ull, 222, 11);
    playerres::PlayerInfo *ghost = playerres::getPlayerInfo(4);
    ghost->ping = 77;
    ghost->loss = 1;
    CHECK(!ghost->isValid());
    putMirror(2, "Mate", 76561197960287930ull, 90, 4);
    playerres::refreshPingLossAll();
    CHECK(ghost->ping == 77);
    CHECK(ghost->loss == 1);
    CHECK(mate->ping == 90); // the valid row followed the mirror
    CHECK(mate->loss == 4);
  }

  TEST_CASE("the refresh is local-agnostic (every rendered row reads)");
  {
    // The IN_SCORE latch lives in the scoreboard/CL_CreateMove layer; the
    // store itself must treat every rendered row the same - the local row
    // included - so pings stream in for all players once the server side
    // starts filling the mirror.
    LocalEntity.index = 2; // mate is the local row now
    putMirror(2, "Mate", 76561197960287930ull, 61, 2);
    putMirror(3, "Other", 76561197960287932ull, 118, 0);
    playerres::updateAll();
    playerres::refreshPingLossAll();
    CHECK(playerres::getPlayerInfo(2)->ping == 61);
    CHECK(playerres::getPlayerInfo(2)->loss == 2);
    CHECK(playerres::getPlayerInfo(3)->ping == 118);
    LocalEntity.index = 0;
  }

  return abctest::finish("abc_ui_ping_refresh");
}
