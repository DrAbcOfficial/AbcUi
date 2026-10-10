// rmlui::KeyMap implementation - see KeyMap.h.
#include "KeyMap.h"

#include <keydefs.h>

namespace rmlui {

Rml::Input::KeyIdentifier mapKeyCode(int keynum) {
	// Normal keys arrive as lowercased ASCII (keydefs.h note).
	if (keynum >= 'a' && keynum <= 'z')
		return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (keynum - 'a'));
	if (keynum >= '0' && keynum <= '9')
		return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (keynum - '0'));

	switch (keynum) {
	case K_SPACE: return Rml::Input::KI_SPACE;
	case K_TAB: return Rml::Input::KI_TAB;
	case K_ENTER: return Rml::Input::KI_RETURN;
	case K_ESCAPE: return Rml::Input::KI_ESCAPE;
	case K_BACKSPACE: return Rml::Input::KI_BACK;
	case K_UPARROW: return Rml::Input::KI_UP;
	case K_DOWNARROW: return Rml::Input::KI_DOWN;
	case K_LEFTARROW: return Rml::Input::KI_LEFT;
	case K_RIGHTARROW: return Rml::Input::KI_RIGHT;
	case K_INS: return Rml::Input::KI_INSERT;
	case K_DEL: return Rml::Input::KI_DELETE;
	case K_PGDN: return Rml::Input::KI_NEXT;
	case K_PGUP: return Rml::Input::KI_PRIOR;
	case K_HOME: return Rml::Input::KI_HOME;
	case K_END: return Rml::Input::KI_END;
	case K_CAPSLOCK: return Rml::Input::KI_CAPITAL;
	case K_KP_SLASH: return Rml::Input::KI_DIVIDE;
	case K_KP_MINUS: return Rml::Input::KI_SUBTRACT;
	case K_KP_PLUS: return Rml::Input::KI_ADD;
	case K_KP_ENTER: return Rml::Input::KI_NUMPADENTER;
	case K_KP_INS: return Rml::Input::KI_NUMPAD0;
	case K_KP_END: return Rml::Input::KI_NUMPAD1;
	case K_KP_DOWNARROW: return Rml::Input::KI_NUMPAD2;
	case K_KP_PGDN: return Rml::Input::KI_NUMPAD3;
	case K_KP_LEFTARROW: return Rml::Input::KI_NUMPAD4;
	case K_KP_5: return Rml::Input::KI_NUMPAD5;
	case K_KP_RIGHTARROW: return Rml::Input::KI_NUMPAD6;
	case K_KP_HOME: return Rml::Input::KI_NUMPAD7;
	case K_KP_UPARROW: return Rml::Input::KI_NUMPAD8;
	case K_KP_PGUP: return Rml::Input::KI_NUMPAD9;
	// The engine delivers the wheel as keynum down-pulses (no matching up),
	// which has no Rml key equivalent - UNKNOWN drops them until wheel
	// scrolling is routed as real wheel events (input batch C).
	case K_MWHEELDOWN: return Rml::Input::KI_UNKNOWN;
	case K_MWHEELUP: return Rml::Input::KI_UNKNOWN;
	default: break;
	}

	if (keynum >= K_F1 && keynum <= K_F12)
		return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + (keynum - K_F1));

	return Rml::Input::KI_UNKNOWN;
}

}
