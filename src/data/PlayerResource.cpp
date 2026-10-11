// data::playerres implementation - see PlayerResource.h.
// from: ABCEnchance/src/core/resource/playerresource.cpp (GBK->UTF-8).
// Studio mirror reads go through hooks::EngineStudioApi (captured from the
// HUD_GetStudioModelInterface chain); donor/admin ride the client's native
// HUD panel table (hooks::NativeHudPanelInfo).
#include <metahook.h>

#include "PlayerResource.h"

#include <com_model.h>
#include <r_studioint.h>

#include <algorithm>
#include <cstdio>

#include "../hooks.h"
#include "../rmlui/SteamAPI.h"
#include "NetworkMessages.h"

namespace playerres {

namespace {

std::array<PlayerInfo, kMaxPlayers + 1>& infos() {
	static std::array<PlayerInfo, kMaxPlayers + 1> Infos = []() {
		std::array<PlayerInfo, kMaxPlayers + 1> array{};
		for (int i = 0; i <= kMaxPlayers; i++)
			array[i].index = i;
		return array;
	}();
	return Infos;
}

// Old PlayerInfo::Update donor read: the native HUD panel table carries the
// donor/admin columns the wire omits.
void readNativeFlags(PlayerInfo& info) {
	const auto* table = hooks::NativeHudPanelInfo;
	info.donor = (info.hideExtra != HideExtra::No && table)
		? static_cast<Donor>(table[info.index - 1].donor)
		: Donor::None;
	// ADMIN rides the wire (ScoreInfo admin byte); the native table's admin
	// column stayed unread by the old code - same here.
}

}

bool PlayerInfo::isThisPlayer() const {
	const cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	return local ? index == local->index : false;
}

void PlayerInfo::update() {
	if (!hooks::EngineStudioApi)
		return;
	const auto* info = reinterpret_cast<const player_info_sc_t*>(
		hooks::EngineStudioApi->PlayerInfo(index - 1));
	const bool wasConnected = connected;
	connected = info != nullptr;
	if (connected != wasConnected) {
		// Player connected or disconnected: drop the stale identity fields
		// (the old g_EventPlayerInfoChanged subscribers saw this state too).
		name.clear();
		realName.clear();
		model.clear();
		steamId64 = 0;
	}
	if (connected) {
		if (steamId64 == 0 || info->m_nSteamID != steamId64) {
			steamId64 = info->m_nSteamID;
			// Local fallback: some engines leave the local slot's SteamID
			// zero (old SteamUser()->GetSteamID() read).
			if (!steamId64) {
				const auto* local = gEngfuncs.GetLocalPlayer();
				if (local && index == local->index)
					steamId64 = steamapi::localSteamId();
			}
			// Old real-name read (GetFriendPersonaName); the active steamapi
			// backend answers, empty when it cannot serve names.
			realName = steamId64 ? steamapi::personaName(steamId64) : std::string();
		}
		topColor = info->topcolor;
		bottomColor = info->bottomcolor;
		ping = info->ping;
		loss = info->packet_loss;
		name = info->name;
		model = info->model;
		readNativeFlags(*this);
	}
}

void PlayerInfo::reset() {
	ping = 0;
	loss = 0;
	model.clear();
	name.clear();
	realName.clear();
	topColor = 0;
	bottomColor = 0;
	frags = 0.0f;
	deaths = 0;
	health = 0.0f;
	armor = 0.0f;
	team = teamres::TeamId::None;
	admin = Admin::None;
	connected = false;
	spectate = false;
	hideExtra = HideExtra::No;
	donor = Donor::None;
	steamId64 = 0;
}

void init() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;
	netmsg::ScoreInfo.append([](int index, float frag, int death, float health, float armor,
		int team, int extra, int admin) {
			PlayerInfo* info = getPlayerInfo(index);
			if (!info)
				return;
			info->frags = frag;
			info->deaths = death;
			info->health = health;
			info->armor = armor;
			info->team = static_cast<teamres::TeamId>(team);
			info->hideExtra = static_cast<HideExtra>(extra);
			info->admin = static_cast<Admin>(admin);
			info->update();
		});
	netmsg::Spectator.append([](int index, bool spectate) {
		PlayerInfo* info = getPlayerInfo(index);
		if (!info)
			return;
		info->spectate = spectate;
	});
	// ClExtrasInfo marks "donor/extra data changed" (the payload was never
	// parsed - old code just re-read the mirrors).
	netmsg::ClExtrasInfo.append([]() {
		updateAll();
	});
}

void resetAll() {
	for (auto& info : infos())
		info.reset();
}

void updateAll() {
	for (auto& info : infos()) {
		if (info.index >= 1)
			info.update();
	}
}

PlayerInfo* getPlayerInfo(int idx) {
	if (idx < 0 || idx > kMaxPlayers)
		return nullptr;
	return &infos()[idx];
}

PlayerInfo* getLocalPlayerInfo() {
	const cl_entity_t* ent = gEngfuncs.GetLocalPlayer();
	if (ent)
		return getPlayerInfo(ent->index);
	return nullptr;
}

bool isEntitySpectate(int idx) {
	const cl_entity_t* plr = gEngfuncs.GetEntityByIndex(idx);
	return plr ? plr->curstate.iuser1 > 0 : false;
}

}
