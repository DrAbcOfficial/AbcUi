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
