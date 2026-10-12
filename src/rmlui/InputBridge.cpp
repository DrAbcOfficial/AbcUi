// rmlui::InputBridge implementation - see InputBridge.h.

#include <metahook.h>
#include <keydefs.h>
#include <abc/Plugin.h>
#include "InputBridge.h"

#include <Windows.h>

#include "KeyMap.h"
#include "RmlUiModule.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {
// Last known mouse position (the WndProc motion and the poll dedup through
// it - the surface cursor position moves with injected SDL motion even when
// no window message is generated).
int LastMouseX = -1;
int LastMouseY = -1;
bool ShiftWasDown = false;
bool CtrlWasDown = false;
bool AltWasDown = false;
// Visibility-gate edge: the poll below only samples while the VGUI cursor is
// visible, so a button HELD across the gate opening (e.g. the MOUSE2 that
// unlocked the scoreboard pointer) must not synthesize a press on the first
// sampled frame.
bool GateWasOpen = false;
// The opening right-click belongs to the scoreboard, not to the UI: the
// MOUSE2 that entered pointer mode is swallowed once (exclusive edge).
bool SuppressOpeningRight = false;
bool DeliveredDown[3] = {};
bool AsyncWasDown[3] = {};

int modifierState() {
	int state = 0;
	if (ShiftWasDown)
		state |= Rml::Input::KM_SHIFT;
	if (CtrlWasDown)
		state |= Rml::Input::KM_CTRL;
	if (AltWasDown)
		state |= Rml::Input::KM_ALT;
	return state;
}

void mouseButton(Rml::Context* context, int button, bool down) {
	if (button == 1 && SuppressOpeningRight) {
		if (!down) SuppressOpeningRight = false;
		return;
	}
	if (down != DeliveredDown[button]) {
		if (down) context->ProcessMouseButtonDown(button, modifierState());
		else context->ProcessMouseButtonUp(button, modifierState());
		DeliveredDown[button] = down;
	}
}

// The router Sink: the Rml key feeds and the engine mouse-mode side effects
// hang here, so ownership transitions are one place (T06: nested owners
// must never fight over IN_Accumulate/cursor state).
class ProductionSink final : public framework::uiinput::Sink {
public:
	void uiKeyDown(int keynum) override {
		if (Rml::Context* context = rmlui::context())
			InputBridge::onKeyEvent(context, 1, keynum);
	}
	void uiKeyUp(int keynum) override {
		if (Rml::Context* context = rmlui::context())
			InputBridge::onKeyEvent(context, 0, keynum);
	}
	void onExclusiveChange(bool exclusive) override {
		if (exclusive) {
			SuppressOpeningRight = true;
			gExportfuncs.IN_DeactivateMouse();
		} else {
			SuppressOpeningRight = false;
			if (auto* context = rmlui::context()) {
				for (int i = 0; i < 3; ++i)
					if (DeliveredDown[i]) context->ProcessMouseButtonUp(i, 0);
				context->ProcessMouseLeave();
			}
			for (int i = 0; i < 3; ++i) DeliveredDown[i] = false;
			gExportfuncs.IN_ActivateMouse();
		}
	}
};

ProductionSink SinkInstance;

} // namespace

framework::uiinput::Router& uiRouter() {
	static framework::uiinput::Router router(&SinkInstance);
	return router;
}

bool uiMouseOwner() {
	return uiRouter().exclusive();
}

bool routeKey(int down, int keynum, bool staticWheelClaim) {
	// Wheel pulses: the top owner (menu scroll) or the static non-owning
	// claims (motd) scroll the UI; anything else stays a weapon switch. The
	// engine delivers wheel as down-only pulses (KeyMap note), but up edges
	// are kept out of the keyboard state machine either way.
	const bool wheel = keynum == K_MWHEELUP || keynum == K_MWHEELDOWN;
	if (down && wheel) {
		if (uiRouter().wheelClaimed() || staticWheelClaim) {
			onWheel(down, keynum);
			return true;
		}
		return false;
	}
	const framework::uiinput::Route route = uiRouter().onKey(down, keynum, wheel);
	return route.consumeEngine;
}

void InputBridge::update(Rml::Context* context) {
	if (!context)
		return;

	updateMouse(context);

	// Modifiers are polled rather than derived from HUD_Key_Event: the engine
	// delivers K_ALT/K_CTRL/K_SHIFT keynums too, but async state stays correct
	// even when a menu swallowed the key stream.
	const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
	// Only tracked for modifierState(); Rml reads modifiers from the state
	// passed alongside key/mouse events, no dedicated process call needed.
	ShiftWasDown = shiftDown;
	CtrlWasDown = ctrlDown;
	AltWasDown = altDown;
}

void InputBridge::updateMouse(Rml::Context* context) {
	// The VGUI cursor visibility doubles as the "GameUI owns the mouse" gate;
	// UI ownership (router exclusivity) opens it regardless - the scoreboard
	// pointer mode's engine-side visibility never flips (P6 finding).
	if (!vgui2ext::isCursorVisible() && !uiRouter().exclusive()) {
		GateWasOpen = false;
		return;
	}

	// Gate edge: sync the held-state baseline silently (no press/release
	// synthesis for buttons held from the game side).
	if (!GateWasOpen) {
		GateWasOpen = true;
		const int virtualKeys[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
		for (int button = 0; button < 3; button++)
			AsyncWasDown[button] = (GetAsyncKeyState(virtualKeys[button]) & 0x8000) != 0;
		LastMouseX = -1; // emit one motion event to seed the hover position
	}

		// Position poll under both paths (the WndProc motion dedups through
	// LastMouseX/Y). The surface cursor coordinates are the context's pixel
	// space: the context dimensions follow the renderer swap chain
	// (RmlUiModule::onVidInit), which is the same coordinate basis the
	// surface reports - no second scaling layer on this path (dp scaling
	// rides the context density, not the input coordinates).
	int x = 0, y = 0;
	if (!vgui2ext::getCursorPos(x, y))
		return;
	if (x != LastMouseX || y != LastMouseY) {
		LastMouseX = x;
		LastMouseY = y;
		context->ProcessMouseMove(x, y, modifierState());
	}

	// Buttons: the WndProc callback writes the same state array as it sees
	// window messages; this async-key sample stays as the fallback for
	// injected input that never surfaces as a window message (P6 acceptance:
	// SDL-injected clicks are invisible to the WndProc chain). Shared state =
	// natural dedup when both paths observe the same button.
	const int virtualKeys[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
	for (int button = 0; button < 3; button++) {
		const bool down = (GetAsyncKeyState(virtualKeys[button]) & 0x8000) != 0;
		if (down != AsyncWasDown[button])
			mouseButton(context, button, down);
		AsyncWasDown[button] = down;
	}
}

bool InputBridge::onKeyEvent(Rml::Context* context, int down, int keynum) {
	if (!context)
		return false;
	// Exclusive mouse keys ride the shared button-dedup state (the
	// IN_MouseEvent edges and the poll land here too).
	if (uiRouter().exclusive() && keynum >= K_MOUSE1 && keynum <= K_MOUSE3) {
		mouseButton(context, keynum - K_MOUSE1, down != 0);
		return true;
	}
	const Rml::Input::KeyIdentifier key = mapKeyCode(keynum);
	if (key == Rml::Input::KI_UNKNOWN)
		return false;
	if (down)
		return context->ProcessKeyDown(key, modifierState());
	return context->ProcessKeyUp(key, modifierState());
}

void onSurfaceWndProc(unsigned int msg, unsigned int wparam, long lparam) {
	// Focus loss precedes the UI gate: no owner may survive into an inactive
	// window (T06 V06). The extension dispatches this callback pre AND post
	// (07 doc item 7) - the router release is idempotent, the second
	// dispatch finds an empty stack and no-ops.
	if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE)) {
		uiRouter().onFocusLost();
		return;
	}

	// Same gate as the legacy poll: normal play keeps the cursor locked and
	// the GameUI menu keeps it visible for its own surface.
	Rml::Context* context = rmlui::context();
	if (!context || (!vgui2ext::isCursorVisible() && !uiRouter().exclusive()))
		return;

	switch (msg) {
		case 0x0200: { // WM_MOUSEMOVE (client coords, signed shorts in lParam)
			const int x = static_cast<short>(LOWORD(lparam));
			const int y = static_cast<short>(HIWORD(lparam));
			LastMouseX = x;
			LastMouseY = y;
			context->ProcessMouseMove(x, y, modifierState());
			break;
		}
		case 0x0201: // WM_LBUTTONDOWN
			mouseButton(context, 0, true);
			break;
		case 0x0202: // WM_LBUTTONUP
			mouseButton(context, 0, false);
			break;
		case 0x0204: // WM_RBUTTONDOWN
			mouseButton(context, 1, true);
			break;
		case 0x0205: // WM_RBUTTONUP
			mouseButton(context, 1, false);
			break;
		case 0x0207: // WM_MBUTTONDOWN
			mouseButton(context, 2, true);
			break;
		case 0x0208: // WM_MBUTTONUP
			mouseButton(context, 2, false);
			break;
		default:
			break; // wheel rides the engine key path (router wheel claims)
	}
}

} // namespace rmlui
