#pragma once

// data::hudstate - local player HUD state the visibility rules hang off:
// suit possession (weapon bits bit 31), hide-HUD tokens, longjump module
// presence and spectate mode. Replaces the scattered state the old Viewport
// kept (m_bitsHideHUDDisplay, m_bitsWeaponBits, the lj/iuser1 checks inside
// its HUD_UpdateClientData handler).
//
// Fed by the hook spine: onUpdateClientData from the HUD_UpdateClientData
// chain, onHideToken from the netmsg HideWeapon/HideHUD events (subscribed
// in init(), HUD_Init-time).

#include <abc/Events.h>

struct client_data_s;

namespace hudstate {

// Hide-HUD token bits (old Viewport.h HUD_HIDEWEAPONS...; the values arrive
// on the HideWeapon/HideHUD messages).
enum HudHideBits {
	kHideWeapons = 1 << 0,
	kHideFlashlight = 1 << 1,
	kHideAll = 1 << 2,
	kHideHealth = 1 << 3,
	kHideSelection = 1 << 4,
	kHideBattery = 1 << 5,
	kHideCustom1 = 1 << 6,
	kHideCustom2 = 1 << 7,
};

extern abc::CallbackList<void(bool)> SuitChanged;
extern abc::CallbackList<void(bool)> LongJumpChanged;
extern abc::CallbackList<void(bool)> SpectateChanged;
// Fires with the raw token whenever HideWeapon/HideHUD arrives (the old
// hudhideCallback ran for both messages).
extern abc::CallbackList<void(int)> HideTokenChanged;

void init();

// Spine feed (HUD_UpdateClientData chain). Tracks suit bits, longjump
// (physinfo "slj") and spectate (local player iuser1), firing the change
// events above.
void onUpdateClientData(const client_data_s* cdata);

bool hasSuit();
bool hasLongJump();
bool isSpectate();
// Raw hide tokens: true when any subscribed message carried the bit.
bool isHudHidden(int token);
// Probe aid: the raw token accumulator.
int debugHideTokens();

}
