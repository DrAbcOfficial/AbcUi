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

// hud.longjump - longjump module possession changed (physinfo "slj").
#define ABCUI_CHANNEL_HUD_LONG_JUMP "hud.longjump"

// hud.spectate - the local player entered/left observer mode.
#define ABCUI_CHANNEL_HUD_SPECTATE "hud.spectate"

// netmsg.health - the server Health value (predicted health rides the hud
// state snapshot; this is the message-fed value).
#define ABCUI_CHANNEL_NETMSG_HEALTH "netmsg.health"

// netmsg.battery - the server Battery value (armor).
#define ABCUI_CHANNEL_NETMSG_BATTERY "netmsg.battery"

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

// hud.chooseWeapon - the weapon-selection highlight moved (slot keys /
// invnext / invprev). The payload is the registry identity (slot, slotPos);
// slot < 0 means "nothing selected" (the highlight walked off the owned
// list).
#define ABCUI_CHANNEL_HUD_CHOOSE_WEAPON "hud.chooseWeapon"
typedef struct AbcUiWeaponEvent {
  int32_t slot;
  int32_t slotPos;
} AbcUiWeaponEvent;

// hud.pickupWeapon / hud.dropWeapon - one registry weapon changed ownership.
// Same payload as hud.chooseWeapon.
#define ABCUI_CHANNEL_HUD_PICKUP_WEAPON "hud.pickupWeapon"
#define ABCUI_CHANNEL_HUD_DROP_WEAPON "hud.dropWeapon"

// hud.dropAllWeapons - every owned weapon went away (payload is null).
#define ABCUI_CHANNEL_HUD_DROP_ALL_WEAPONS "hud.dropAllWeapons"

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

// netmsg.showmenu - vanilla ShowMenu (number-key menu stream). `text` is a
// dispatch-scoped pointer: copy before the sink returns. Chunked menus
// arrive as several events (bits & 0x7f = more chunks pending, bit 7 =
// ammo-selection menu).
#define ABCUI_CHANNEL_NETMSG_SHOW_MENU "netmsg.showmenu"
typedef struct AbcUiShowMenuEvent {
  int32_t validSlots;
  int32_t displaySeconds;
  int32_t bits;
  const char *text;
} AbcUiShowMenuEvent;

// netmsg.inithud - the client re-ran its HUD init (payload is null).
#define ABCUI_CHANNEL_NETMSG_INIT_HUD "netmsg.inithud"

// netmsg.votemenu - the server opened a yes/no vote; `type` is the vanilla
// vote enum (1 kill, 2 kick, 3 ban, 4 map, ...). The three strings are
// dispatch-scoped pointers: copy before the sink returns. The message is
// only routed while the hud_vote gate is on (console->setVoteGate).
#define ABCUI_CHANNEL_NETMSG_VOTE_MENU "netmsg.votemenu"
typedef struct AbcUiVoteMenuEvent {
  int32_t type;
  const char *content; // server caption text
  const char *yes;     // server-provided yes caption, "" when absent
  const char *no;      // server-provided no caption, "" when absent
} AbcUiVoteMenuEvent;

// netmsg.endvote - the vote window closed (payload is null).
#define ABCUI_CHANNEL_NETMSG_END_VOTE "netmsg.endvote"

// netmsg.motd - one MOTD chunk from the server. `text` is a dispatch-scoped
// pointer; an empty text means "close the window" (old empty-chunk
// semantics) and code > 0 marks the final chunk of the message.
#define ABCUI_CHANNEL_NETMSG_MOTD "netmsg.motd"
typedef struct AbcUiMotdEvent {
  int32_t code;
  const char *text;
} AbcUiMotdEvent;

// menu.vguimenu - the client asked for a native viewport window
// (CTeamFortressViewport::ShowVGUIMenu ids: 4 mission brief, 5 MOTD, ...).
// Veto channel: nonzero suppresses the vanilla window.
#define ABCUI_CHANNEL_MENU_VGUI_MENU "menu.vguimenu"
typedef struct AbcUiVguiMenuEvent {
  int32_t menu;
} AbcUiVguiMenuEvent;

// input.key - one HUD key event. Veto channel: a sink returning nonzero
// consumes the press so the engine binding chain never sees it (the vote
// keys answer the vote instead of running their binding).
#define ABCUI_CHANNEL_INPUT_KEY "input.key"
typedef struct AbcUiKeyEvent {
  int32_t down;   // 1 key down, 0 key up
  int32_t keynum; // engine key number
} AbcUiKeyEvent;

// input.wheel - one mouse-wheel pulse (AbcUiKeyEvent payload). Veto channel
// with claim semantics: nonzero routes the pulse into the Rml context (the
// open document scrolls) without taking input ownership; zero leaves it to
// the engine bindings (weapon switch).
#define ABCUI_CHANNEL_INPUT_WHEEL "input.wheel"

// textfeed.killfeed - one routed kill line. All three strings are
// dispatch-scoped pointers; `attacker` is empty for a suicide. `inflictor`
// is the localized weapon phrase (the old kill feed's bracket text).
#define ABCUI_CHANNEL_TEXTFEED_KILL_FEED "textfeed.killfeed"
typedef struct AbcUiKillFeedEvent {
  const char *victim;
  const char *attacker;
  const char *inflictor;
} AbcUiKillFeedEvent;

// textfeed.notice - one routed centerprint/notify line. `text` is a
// dispatch-scoped pointer; `target` is the old HUDNOTICE value (1 notify,
// 4 center).
#define ABCUI_CHANNEL_TEXTFEED_NOTICE "textfeed.notice"
typedef struct AbcUiNoticeEvent {
  int32_t target;
  const char *text;
} AbcUiNoticeEvent;
