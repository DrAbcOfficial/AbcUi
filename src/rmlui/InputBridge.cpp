// rmlui::InputBridge implementation - see InputBridge.h.
#include <metahook.h>

#include "InputBridge.h"

#include <Windows.h>

#include "KeyMap.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {
// Last known mouse button states (VK_LBUTTON/VK_RBUTTON/VK_MBUTTON order).
bool MouseButtonWasDown[3] = {};
bool ShiftWasDown = false;
bool CtrlWasDown = false;
bool AltWasDown = false;

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
	// The VGUI cursor visibility doubles as the "GameUI owns the mouse" gate:
	// during normal play (cursor hidden, engine shooting) Rml must not see
	// clicks, and the GameUI menu activates the VGUI cursor.
	if (!vgui2ext::isCursorVisible())
		return;

	int x = 0, y = 0;
	if (!vgui2ext::getCursorPos(x, y))
		return;
	context->ProcessMouseMove(x, y, modifierState());

	// Buttons via async state; surface ids: 0 left, 1 right, 2 middle (Rml).
	const int virtualKeys[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
	for (int button = 0; button < 3; button++) {
		const bool down = (GetAsyncKeyState(virtualKeys[button]) & 0x8000) != 0;
		if (down && !MouseButtonWasDown[button])
			context->ProcessMouseButtonDown(button, modifierState());
		else if (!down && MouseButtonWasDown[button])
			context->ProcessMouseButtonUp(button, modifierState());
		MouseButtonWasDown[button] = down;
	}
}

void InputBridge::onKeyEvent(Rml::Context* context, int down, int keynum) {
	if (!context)
		return;
	const Rml::Input::KeyIdentifier key = mapKeyCode(keynum);
	if (key == Rml::Input::KI_UNKNOWN)
		return;
	if (down)
		context->ProcessKeyDown(key, modifierState());
	else
		context->ProcessKeyUp(key, modifierState());
}

}
