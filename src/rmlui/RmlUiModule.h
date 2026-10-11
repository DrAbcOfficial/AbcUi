#pragma once

// rmlui - RmlUi assembly module. Plan 05 §2/§3.
//
// Startup order (LoadClient): abc::MetaRendererInit (caller) -> vgui2ext
// import -> Rml::Initialise (render/system/file interfaces) -> context ->
// fonts -> theme assets -> panels -> debugger -> RegisterRenderCallbacks.
//
// Render timing: IMetaRendererCallbacks::OnRenderEndFrame drives the Rml
// render pass - fires exactly once per frame (GL_EndRendering ->
// R_RenderEndFrame), after all built-in post-processing and immediately
// before the final buffer is blitted to screen. This is the "implementation
// micro-adjustment" the plan anticipated vs OnPostRenderView: the latter
// fires per view render (water portals/mirrors would double-draw the UI).

namespace Rml {
class Context;
}

namespace rmlui {

class AbcRenderInterface;

// Deferred assembly: the first HUD_Frame builds the Rml context (one-shot).
// LoadClient only grabs the renderer/VGUI2 interfaces (both proven safe
// there); the context itself cannot be built at LoadClient - the VGUI
// surface texture channel the font engine needs is not usable that early,
// and with -windowed the chained HUD_VidInit fires before the export patch
// lands (both Phase 3 trace findings).
void init();
// Idempotent; safe from both HUD_Shutdown and ExitGame.
void shutdown();

// HUD_Init time: abc_ui_debugger command (cvar/command red line, plan 08 §4.9).
void registerCommands();

// Live Rml context, null before init()/after shutdown() - Debug probe
// tooling only (ProbeCommand.cpp).
Rml::Context* context();

// Live render interface, null before init()/after shutdown(). The radar
// panel registers its FBO as an external masked texture through it (batch D).
AbcRenderInterface* renderInterface();

// Spine entries (hooks.cpp).
void onFrame(double frameTime);
// HUD_Redraw chain: projection consumers reposition + second Update.
void onRedraw(double frameTime);
void onVidInit();
void onKeyEvent(int down, int keynum);
// Engine wheel pulses (HUD_Key_Event keynums) -> Context::ProcessMouseWheel;
// called while a scrollable UI consumer owns the wheel.
void onWheel(int down, int keynum);
// Engine surface WndProc messages (vgui2ext BaseUI callback, R-4 batch C):
// UI-level mouse motion/buttons for the Rml context. Observation-only.
void onSurfaceWndProc(unsigned int msg, unsigned int wparam, long lparam);
// InputBridge pointer-mode ownership (scoreboard setMouseMode): forces the
// mouse gate open even while the engine-side cursor visibility stays hidden.
void setUiMouseOwner(bool owner);
bool uiMouseOwner();
// HUD_Shutdown: full teardown (context/docs die with the client state).
void onHudShutdown();

}
