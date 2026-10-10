#pragma once

// vgui2ext - VGUI2Extension.dll import helper (R-4 source of truth).
// Mirrors Renderer's src/VGUI2ExtensionImport.cpp pattern: resolve the plugin
// module + factories once at LoadClient, expose only what AbcUi consumes.
//
// R-4 verdict (Phase 3): IVGUI2Extension.h lives in the pinned VGUI2Extension
// submodule (include/Interface/, API v010). Mouse capability: full WM-level
// events via IVGUI2Extension_BaseUICallbacks::CallEngineSurfaceWndProc, or
// polling via ISurface2 (SurfaceGetCursorPos/IsCursorVisible). Phase 3 polls;
// batch C re-evaluates the callback path (plan 05 §3.5).
//
// Everything degrades to null when VGUI2Extension.dll is absent: texture
// uploads and cursor polling then report unavailable (plan 05 §3.5 fallback:
// keyboard-only panels).

namespace vgui2ext {

// LoadClient time; safe to call when the plugin is missing.
void init();
void shutdown();

// True when the ISurface2 interface was resolved.
bool available();

// Texture channel (ids live in the surface texture namespace that
// IMetaRenderer's DrawTexturedRect consumes). Returns 0 when unavailable.
int createTexture();
void uploadTexture(int textureId, const unsigned char* rgba, int width, int height);
void deleteTextureById(int textureId);

// Cursor state for InputBridge (window/pixel coords, y-down). Returns false
// when unavailable.
bool getCursorPos(int& x, int& y);
bool isCursorVisible();

// Game language setting ("english"/"schinese"/...), empty when unavailable.
const char* currentLanguage();

}
