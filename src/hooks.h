#pragma once

// AbcUi hook spine: chains the client exports this plugin drives into the
// export table (R-2 chain semantics, same shape as AbcEssential src/hooks.cpp).
// Consumed exports: HUD_Init (command registration), HUD_VidInit (context
// resize), HUD_Frame (Rml update pump + input poll), HUD_Key_Event (keyboard
// into Rml), HUD_Shutdown (teardown). Rendering is NOT driven from an export:
// the Rml render pass runs in IMetaRendererCallbacks::OnRenderEndFrame (see
// rmlui/RmlUiModule) - exactly once per frame, right before the final buffer
// is blitted to screen.
//
// No cvar/command registration may happen before HUD_Init (plan 08 §4.9).

#include <metahook.h>

namespace hooks {

// LoadClient time: chain our HUD_XXX implementations into the export table.
void attachExports(cl_exportfuncs_t* pExportFuncs);

}
