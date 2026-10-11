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
// Last known mouse button states (VK_LBUTTON/VK_RBUTTON/VK_MBUTTON order).
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
// Explicit ownership (scoreboard pointer mode): the engine-side cursor
// visibility never flips for it (P6 acceptance finding), so the mode carries
// its own gate bit.
bool UiMouseOwner = false;
bool SuppressOpeningRight = false;
bool DeliveredDown[3] = {};
bool AsyncWasDown[3] = {};

int modifierState();

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

} // namespace

void setUiMouseOwner(bool owner) {
	if (owner == UiMouseOwner) return;
	UiMouseOwner = owner;
	GateWasOpen = false;
	SuppressOpeningRight = owner;
	if (owner) {
		gExportfuncs.IN_DeactivateMouse();
	} else {
		if (auto* context = rmlui::context()) {
			for (int i = 0; i < 3; ++i)
				if (DeliveredDown[i]) context->ProcessMouseButtonUp(i, 0);
			context->ProcessMouseLeave();
		}
		for (int i = 0; i < 3; ++i) DeliveredDown[i] = false;
		gExportfuncs.IN_ActivateMouse();
	}
}

bool uiMouseOwner() {
	return UiMouseOwner;
}

namespace {
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
} // namespace

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
	// the scoreboard pointer mode carries its own bit (engine visibility
	// never flips for it).
	if (!vgui2ext::isCursorVisible() && !UiMouseOwner) {
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
	// LastMouseX/Y - the surface cursor position moves with injected SDL
	// motion even when no window message is generated).
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
	if (UiMouseOwner && keynum >= K_MOUSE1 && keynum <= K_MOUSE3) {
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
	// Same gate as the legacy poll: normal play keeps the cursor locked and
	// the GameUI menu keeps it visible for its own surface.
	Rml::Context* context = rmlui::context();
	if (!context || (!vgui2ext::isCursorVisible() && !UiMouseOwner))
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
			break; // wheel rides the engine key path (hooks uiOwnsWheel)
	}
}

} // namespace rmlui
