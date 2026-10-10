#pragma once

// rmlui::KeyMap - engine keynums (HLSDK keydefs.h + lowercased ASCII) ->
// Rml::Input::KeyIdentifier. Plan 05 §3.5.
//
// RmlUi's KeyIdentifier follows Win32 VK codes; the GoldSrc numbering is
// different (K_TAB=9 by coincidence, K_ENTER=13, K_ESCAPE=27, arrows at
// 128+...), so a table maps the set the UI consumes.

#include <RmlUi/Core/Input.h>

namespace rmlui {

// Unknown keynums map to KI_UNKNOWN.
Rml::Input::KeyIdentifier mapKeyCode(int keynum);

}
