#pragma once

// rmlui::InputBridge - game input -> Rml context. Plan 05 §3.5.
//
// Keyboard: HUD_Key_Event spine hook (rmlui::onKeyEvent) feeds
// ProcessKeyDown/Up. Modifier state (shift/ctrl/alt) is tracked locally from
// the same stream (the Win32-backend GetKeyModifierState semantics).
//
// Mouse (Phase 3): polled once per HUD_Frame - position from the VGUI2
// surface, buttons from GetAsyncKeyState while the VGUI cursor is visible.
// Batch C upgrades this to the IVGUI2Extension WndProc callback when real
// event timing matters (R-4 note in VGUI2ExtensionImport.h). While the VGUI
// cursor is hidden (normal play, engine cursor) Rml receives no mouse input.

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

namespace rmlui {

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
