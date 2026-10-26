#pragma once

// framework::standaloneview - host glue for the pinned
// thirdparty/StandaloneView module (issue #24 comment): render isolated 3D
// views (the model preview) through the ENGINE's own V_RenderView, with or
// without Renderer, instead of Renderer's IEntityViewRenderer isolated
// scope.
//
// Consumers implement the module's IStandaloneView directly and register
// through its StandaloneViewManager() singleton. This layer only carries
// what the module cannot do for this plugin:
// - the hook glue: soft gamedata degradation instead of the module's
//   SysError, and client-export routing through hooks.cpp's chain
//   (gExportfuncs) rather than a replaced export table;
// - the studio draw bridge: the module's IStandaloneViewDrawfuncs cannot
//   express a private MOVETYPE_FOLLOW bone source or a per-draw directional
//   light, so setupStudioPose / drawStudioModel are concrete functions for
//   the view's triangle callbacks, with StudioLighting as their per-draw
//   light configuration, hooked in at the engine's StudioSetupLighting seam;
// - NoVisScope: the module's empty world carries no BSP visibility data, so
//   the client's StudioDrawModel culls the preview entity while it is
//   installed; arm the scope for the duration of a view's pass.

struct ref_params_s;
struct cl_entity_s;
struct cvar_s;
struct r_studio_interface_s;
struct engine_studio_api_s;

#include <array>

namespace framework {
namespace standaloneview {

// Studio light-ray direction in world coordinates (opposite the direction
// from the model towards the light). Applied only during the requested draw.
struct StudioLighting {
  int Ambient = 96;
  int Shade = 96;
  std::array<float, 3> Color{1, 1, 1};
  std::array<float, 3> Direction{-1, 0, 0};
  bool Enabled = true;
};

// Evaluate/save the merged pose without culling, meshes or events. The
// engine Studio bone transforms remain readable until the next Studio call.
bool setupStudioPose(cl_entity_s *ent, cl_entity_s *boneSource = nullptr);

// StudioDrawModel with STUDIO_RENDER and no STUDIO_EVENTS (a preview must
// not fire model events), currententity and r_blend set and restored the
// way the engine's entity lists do. For a private MOVETYPE_FOLLOW entity,
// pass its bone source explicitly: these entities are not registered in the
// engine's entity array. The source is evaluated with flags=0 immediately
// before the draw (bones only, no mesh/events).
void drawStudioModel(cl_entity_s *ent, cl_entity_s *boneSource = nullptr,
                     const StudioLighting *lighting = nullptr);

// Rides the engine's no-visibility path while armed. The scope arms only
// while the manager's empty world is actually installed, so on a real map
// the main view sees the player's own r_novis.
class NoVisScope {
public:
  NoVisScope();
  ~NoVisScope();
  NoVisScope(const NoVisScope &) = delete;
  NoVisScope &operator=(const NoVisScope &) = delete;

private:
  cvar_s *Cvar = nullptr;
  float Saved = 0.0f;
};

// True when the route installed (engine hooks resolved +, on a Sven client,
// portal hooks resolved). Consumers degrade when false.
bool available();

// OnLoadClient: engine hooks once per process. Portal hooks also re-arm on
// HUD_Init after each HUD_Shutdown. Repeated installs are idempotent.
// Soft-degrading - see the header note.
void installEngineHooks();
void installClientHooks();
// HUD_Shutdown: remove portal hooks before a possible client unload, but
// retain the studio slot across ordinary HUD cycles. A real LoadClient
// clears that slot with captureStudioInterfaces(nullptr, nullptr).
void uninstallClientHooks();

// hooks.cpp export routing: consumed returns true when a view's pass owns
// the call and the client original must be skipped.
bool hookCalcRefdef(ref_params_s *pparams);
bool hookDrawNormalTriangles();
bool hookDrawTransparentTriangles();

// hooks.cpp H_HUD_GetStudioModelInterface: capture the studio interface the
// engine received (the client's, or the engine's own when the client
// provides none) and the engine studio API. The engine-owned slot is read
// at draw time because the engine may fill its fallback after this export.
void captureStudioInterfaces(r_studio_interface_s **ppinterface,
                             engine_studio_api_s *pstudio);

} // namespace standaloneview
} // namespace framework
