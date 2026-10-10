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

namespace rmlui {

// LoadClient time. Fatals through abc::SysError when Renderer is missing
// (fatal Renderer check is a plugin-level decision, plan 05 §2).
void init();
// Idempotent; safe from both HUD_Shutdown and ExitGame.
void shutdown();

// HUD_Init time: abc_ui_debugger command (cvar/command red line, plan 08 §4.9).
void registerCommands();

// Spine entries (hooks.cpp).
void onFrame(double frameTime);
void onVidInit();
void onKeyEvent(int down, int keynum);
// HUD_Shutdown: full teardown (context/docs die with the client state).
void onHudShutdown();

}
