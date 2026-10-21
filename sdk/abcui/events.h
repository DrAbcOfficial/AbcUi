#pragma once

// sdk/abcui/events.h - event bus channel constants and payload PODs.
//
// Channel ids are lowercase dotted strings owned by the publishing
// subsystem; every channel documents its payload struct here. Payloads are
// C PODs with a stable layout (fixed-size character arrays sized to the
// engine's limits) so they cross the pure-C module ABI. A sink receives a
// const pointer valid for the dispatch only - copy out anything kept.
//
// Naming:
//   netmsg.<message>  - hooked engine user messages (raw parse results)
//   textfeed.<feed>   - routed text feeds (kill feed, notices)
//   hud.<state>       - HUD state changes (health, battery, flashlight, ...)
//   input.<operation> - user operations (key events, mouse, commands)
//   client.<event>    - engine client lifecycle pumps (frame, redraw, ...)
//   menu.<event>      - native GameUI/BaseUI takeover events
//
// Veto channels (documented per constant) stop at the first sink returning
// nonzero - a higher-priority interception hides the event from everything
// below it.

#include "module_api.h"

// netmsg.flashlight - flashlight toggled; battery 0..100.
#define ABCUI_CHANNEL_NETMSG_FLASHLIGHT "netmsg.flashlight"
typedef struct AbcUiFlashlightEvent {
  int32_t on;
  int32_t battery;
} AbcUiFlashlightEvent;

// netmsg.flashbat - flashlight battery update.
#define ABCUI_CHANNEL_NETMSG_FLASHBAT "netmsg.flashbat"
typedef struct AbcUiFlashBatEvent {
  int32_t battery;
} AbcUiFlashBatEvent;

// hud.suit - HEV suit possession changed (weapon bits bit 31).
#define ABCUI_CHANNEL_HUD_SUIT "hud.suit"
typedef struct AbcUiBoolEvent {
  int32_t value;
} AbcUiBoolEvent;

// hud.hideToken - raw HideWeapon/HideHUD token, last write wins. Bits:
#define ABCUI_CHANNEL_HUD_HIDE_TOKEN "hud.hideToken"
typedef struct AbcUiIntEvent {
  int32_t value;
} AbcUiIntEvent;

#define ABCUI_HIDEBIT_WEAPONS (1 << 0)
#define ABCUI_HIDEBIT_FLASHLIGHT (1 << 1)
#define ABCUI_HIDEBIT_ALL (1 << 2)
#define ABCUI_HIDEBIT_HEALTH (1 << 3)
#define ABCUI_HIDEBIT_SELECTION (1 << 4)
#define ABCUI_HIDEBIT_BATTERY (1 << 5)
#define ABCUI_HIDEBIT_CUSTOM1 (1 << 6)
#define ABCUI_HIDEBIT_CUSTOM2 (1 << 7)

// HUD_Frame pump payload; also the "module ran this frame" heartbeat.
#define ABCUI_CHANNEL_FRAME "client.frame"
typedef struct AbcUiFrameEvent {
  double frameTime; // seconds, engine frame time
} AbcUiFrameEvent;

// HUD_Redraw pump payload (view matrices live; projection phase).
#define ABCUI_CHANNEL_REDRAW "client.redraw"
typedef struct AbcUiRedrawEvent {
  double frameTime;
} AbcUiRedrawEvent;
