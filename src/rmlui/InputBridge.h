#pragma once

// rmlui::InputBridge - game input -> Rml context. Plan 05 §3.5, T06 rework.
//
// ONE ownership truth: framework::uiinput::Router (tiered token stack,
// T06). The production router instance and its Sink live here (uiRouter());
// owners acquire/release through RAII tokens - the scoreboard pointer mode,
// the menu page, later console/capture tiers. rmlui::uiMouseOwner() reads
// router exclusivity for the engine-side gates (IN_Accumulate /
// IN_MouseEvent / CL_CreateMove in hooks.cpp).
//
// Event sources (T06 step-1 table; full pre/post + return semantics in
// execution/T06.md):
// - Keyboard / mouse keys / wheel: HUD_Key_Event export chain (hooks.cpp)
//   -> routeKey() is the single physical injection point.
// - Client IN_MouseEvent edges: fed directly while exclusive (the client
//   generates its own key edges there; the shared DeliveredDown dedup
//   state makes the overlap with the key stream idempotent).
// - Surface WndProc (VGUI2Extension BaseUI callback): mouse motion/buttons
//   plus focus-loss observation. The extension dispatches this callback
//   pre AND post (07 doc item 7) - every handler below is state-deduped or
//   idempotent, and nothing here ever consumes the message.
// - Async poll (HUD_Frame): position + button fallback for injected input
//   invisible to the WndProc chain (P6 acceptance finding).
// - BaseUI Key_Event callback: stays a no-op. The engine only routes keys
//   there while the original GameUI owns the key destination; wiring it is
//   T08's takeover seam. Choosing one physical injection entry IS the
//   dual-callback dedup; when T08 adds the second source it must land in
//   routeKey, not beside it.
//
// Mouse: the gate is the VGUI cursor visibility OR router exclusivity; the
// fallback poll (no VGUI2Extension) keeps the Phase 3 shape.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

#include "../framework/UiInputRouter.h"

namespace rmlui {

// Production router instance (ownership stack + keyboard state machine).
// The Sink side effects (Rml key feeds, engine mouse mode, opening-click
// suppression) are bound to it here.
framework::uiinput::Router& uiRouter();

// Kept for the engine-side gates and diagnostics: true while any owner
// holds UI input (scoreboard pointer mode, menu page, ...).
bool uiMouseOwner();

// The single physical-event injection point (hooks.cpp H_HUD_Key_Event).
// Routes the edge through the ownership router (which feeds the UI what it
// orders), routes wheel pulses to onWheel when claimed, and returns the
// engine-consume decision (true = HUD_Key_Event returns 0). The pre-stage
// HUD consumers (vote answers, scoreboard pointer enter) run BEFORE this
// in hooks.cpp; `staticWheelClaim` carries their non-owning remainder
// (motd scroll).
bool routeKey(int down, int keynum, bool staticWheelClaim);

class InputBridge {
public:
	// HUD_Frame time, before Context::Update.
	static void update(Rml::Context* context);

	// Raw UI-side key feed (also the router Sink body): mouse keys ride the
	// shared button-dedup state, everything else maps through KeyMap into
	// ProcessKeyDown/Up. Returns the Rml consume flag (diagnostic only - no
	// consumption decision may read it, T06 step 3).
	static bool onKeyEvent(Rml::Context* context, int down, int keynum);

private:
	static void updateMouse(Rml::Context* context);
};

}
