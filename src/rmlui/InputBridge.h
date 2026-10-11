#pragma once

// rmlui::InputBridge - game input -> Rml context. Plan 05 §3.5.
//
// Keyboard: HUD_Key_Event spine hook (rmlui::onKeyEvent) feeds
// ProcessKeyDown/Up. Modifier state (shift/ctrl/alt) is tracked locally from
// the same stream (the Win32-backend GetKeyModifierState semantics).
//
// Mouse (batch C): the IVGUI2Extension WndProc callback drives clicks and
// motion (R-4; the async-key poll cannot see injected/message-level input).
// The gate is the VGUI cursor visibility OR the explicit UI-mouse owner flag
// (scoreboard pointer mode - the old vgui SetMouseInputEnabled, engine-side
// cursor visibility never flips for it). While neither holds, Rml receives
// no mouse input. The fallback poll (no VGUI2Extension) keeps the Phase 3
// shape.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

namespace rmlui {

// Pointer-mode ownership (scoreboard setMouseMode): forces the mouse gate
// open even while the engine-side cursor visibility stays hidden.
void setUiMouseOwner(bool owner);
bool uiMouseOwner();

class InputBridge {
public:
	// HUD_Frame time, before Context::Update.
	static void update(Rml::Context* context);

	// HUD_Key_Event spine entry (down: 1 press / 0 release; keynum: keydefs).
	// Returns the Rml consume flag (true = an element handled the key).
	static bool onKeyEvent(Rml::Context* context, int down, int keynum);

private:
	static void updateMouse(Rml::Context* context);
};

}
