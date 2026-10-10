#pragma once

// framework::Theme - HUD theme/stylesheet conventions and the hud_size /
// hud_alpha knobs. Plan 05 §4.3.
//
// RmlUi 6 attaches stylesheets through <link> tags in each document's <head>
// (no context-level sheet API in 6.3), so Theme's contract here is: every
// HudPanel document links kBaseSheetPath first, then its own sheet. hud_size
// scaling (root class switch + rem units) and hud_alpha (RCSS variable) land
// with batch A panels when there is a real hierarchy to scale - those calls
// will hang off this class.

namespace framework {

namespace Theme {

// Shared sheet every panel document links (fonts, colours, hud vars).
constexpr const char* kBaseSheetPath = "abcui/rcss/base.rcss";

}

}
