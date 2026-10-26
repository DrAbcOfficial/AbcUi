// framework::standaloneview host glue - see StandaloneView.h.
//
// The route's manager core (view list, forced client state, the empty brush
// world, the paint-background gate, the render loop) is the pinned
// thirdparty/StandaloneView submodule (MIT, (c) 2021 hzqst), compiled into
// this DLL (StandaloneViewManager.cpp + EmptyWorld.cpp). This file plays the
// role upstream's plugins.cpp/privatehook.cpp play for the standalone
// plugin, with the AbcUi adaptations:
// - soft gamedata degradation instead of SysError: an unresolvable symbol
//   set on an unknown build disables the route, the preview falls into its
//   "preview disabled" branch instead of failing startup;
// - the client-export routing goes through hooks.cpp's chain (gExportfuncs)
//   rather than replacing the client's export table;
// - the draw bridge is studio-only and extended by the preview's needs the
//   upstream interface does not model: a private MOVETYPE_FOLLOW bone
//   source and a per-draw StudioLighting override hooked at the engine's
//   StudioSetupLighting seam;
// - the empty-world pass rides r_novis: the empty world carries no BSP
//   visibility data, so the client's StudioDrawModel PVS test would cull
//   the preview entity and the pass would draw nothing.
#include <metahook.h>

#include <com_model.h>
#include <const.h>
#include <demo_api.h>
#include <r_studioint.h>
#include <ref_params.h>
#include <studio.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include <EmptyWorld.h>
#include <IStandaloneView.h>
#include <StandaloneViewManager.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

#include "StandaloneView.h"

namespace framework {
namespace standaloneview {

// The submodule's namespace (::standaloneview, next to this facade's
// framework::standaloneview).
namespace sv = ::standaloneview;

namespace {

// Engine functions and globals resolved from gamedata: the state slots the
// manager renders against plus the entity-draw globals the studio bridge
// reads.
struct EngineBridge {
  int *clientState = nullptr;         // cls.state
  int *clientSignon = nullptr;        // cls.signon
  model_s **worldModel = nullptr;     // cl_worldmodel
  cl_entity_s *worldEntity = nullptr; // r_worldentity
  int (*CL_FxBlend)(cl_entity_s *ent) = nullptr;
  cl_entity_s **currententity = nullptr;
  float *r_blend = nullptr;
};

EngineBridge Engine;
bool EngineHooksInstalled = false;
bool Available = false;
bool ClientHooksReady = false;

// The engine's r_studio_interface_t and engine studio API, captured from
// HUD_GetStudioModelInterface (hooks.cpp forwards them).
r_studio_interface_t **StudioInterface = nullptr;
engine_studio_api_t *EngineStudio = nullptr;

hook_t *RenderViewHook = nullptr;
hook_t *PaintBackgroundHook = nullptr;
hook_t *RenderPortalsHook = nullptr;
hook_t *ResetAllHook = nullptr;
hook_t *ThirdPersonHook = nullptr;
void (*OriginalRenderView)() = nullptr; // read per call, NOT cached in a
                                        // bridge: MetaHook writes the
                                        // trampoline only after every
                                        // plugin's LoadClient returned
using PaintBackgroundFn = void (*)(int *extents);
PaintBackgroundFn OriginalPaintBackground = nullptr;
using RenderPortalsFn = void(__fastcall *)(void *pthis, int dummy,
                                           ref_params_s *pparams);
RenderPortalsFn OriginalRenderPortals = nullptr;
using ResetAllFn = int(__fastcall *)(void *pthis, int dummy);
ResetAllFn OriginalResetAll = nullptr;
int (*OriginalThirdPerson)() = nullptr;

// ------------------------------------------------- manager feed shims ----

// The manager drives every V_RenderView call - one per active view plus the
// main view - through these per-call trampoline shims (see OriginalRenderView).
void callOriginalRenderView() {
  if (OriginalRenderView)
    OriginalRenderView();
}

void paintBackgroundTrampoline(int *extents) {
  if (OriginalPaintBackground)
    OriginalPaintBackground(extents);
}

bool isDemoPlayback() {
  return gEngfuncs.pDemoAPI && gEngfuncs.pDemoAPI->IsPlayingback();
}

// The manager reports consumer contract violations through EngineBridge::
// SysError (a fatal for the standalone plugin). They are wiring bugs of
// this DLL, so they degrade to console warnings like every other soft
// failure here.
void misuseReport(const char *fmt, ...) {
  char message[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  abc::ConsoleWarn("[AbcUi] standalone view: %s\n", message);
}

// The brush and sprite entity draws stay unresolved on purpose: the
// preview's only consumer draws through the studio bridge below (the
// facade's Drawfuncs is studio-only by contract), and every skipped symbol
// widens the set of builds the route degrades gracefully on. The manager
// null-checks both entries.
sv::EngineBridge makeManagerBridge() {
  return sv::EngineBridge{
      .V_RenderView = callOriginalRenderView,
      .IsDemoPlayback = isDemoPlayback,
      .clientState = Engine.clientState,
      .clientSignon = Engine.clientSignon,
      .worldModel = Engine.worldModel,
      .worldEntity = Engine.worldEntity,
      .SysError = misuseReport,
  };
}

// ------------------------------------------------------------ hook bodies -

void __cdecl RenderViewHookFn() {
  // The manager bridge is always set when this runs: the inline hooks are
  // installed only after the resolve and the SetEngine feed succeeded.
  sv::StandaloneViewManager().RenderView();
}

void __cdecl PaintBackgroundHookFn(int *extents) {
  sv::StandaloneViewManager().PaintBackground(paintBackgroundTrampoline,
                                              extents);
}

// ------------------------------------------------------------------ hooks -

// Soft resolution: the issue comment's degradation rule - an unknown engine
// build disables the route instead of failing startup.
bool resolve(void *module, const char *name, mh_gamesymbol_kind_t kind,
             void **address) {
  if (!module)
    return false;
  const auto status =
      g_pMetaHookAPI->ResolveGameSymbol(module, name, kind, address);
  if (status == MH_GAMESYMBOL_OK && *address != nullptr)
    return true;
  abc::ConsoleWarn("[AbcUi] standalone view symbol unavailable: %s "
                   "(status %d)\n",
                   name, static_cast<int>(status));
  return false;
}

bool isSvenCoopClient() {
  const auto clientFactory = g_pMetaHookAPI->GetClientFactory();
  return clientFactory && clientFactory("SCClientDLL001", nullptr) != nullptr;
}

void uninstallPortalHooks() {
  if (ThirdPersonHook) {
    g_pMetaHookAPI->UnHook(ThirdPersonHook);
    ThirdPersonHook = nullptr;
  }
  if (ResetAllHook) {
    g_pMetaHookAPI->UnHook(ResetAllHook);
    ResetAllHook = nullptr;
  }
  if (RenderPortalsHook) {
    g_pMetaHookAPI->UnHook(RenderPortalsHook);
    RenderPortalsHook = nullptr;
  }
  OriginalResetAll = nullptr;
  OriginalThirdPerson = nullptr;
  OriginalRenderPortals = nullptr;
}

// SvEngine's V_RenderView lets the Sven client render its portals before
// every pass; a standalone pass must not re-render them, and without a map
// the client's portal state is invalid (main-menu crashes without these
// hooks: RenderPortals dereferences map state, ResetAll calls GL entry
// points the client initializes only on map load).
// The portal hook bodies (thiscall via __fastcall + dummy).
void __fastcall portalRenderPortalsHook(void *pthis, int,
                                        ref_params_s *pparams) {
  if (sv::StandaloneViewManager().GetCurrentView())
    return;
  if (OriginalRenderPortals)
    OriginalRenderPortals(pthis, 0, pparams);
}

int __fastcall portalResetAllHook(void *pthis, int) {
  if (sv::StandaloneViewManager().GetCurrentView())
    return 0;
  if (OriginalResetAll)
    return OriginalResetAll(pthis, 0);
  return 0;
}

int __cdecl thirdPersonHook() {
  // This is an external view, never the client's first-person body pass.
  // Otherwise Sven consumes its pending local-player draw here: there is no
  // local player at the menu, and in a level it belongs to the real view.
  if (sv::StandaloneViewManager().GetCurrentView())
    return 1;
  return OriginalThirdPerson ? OriginalThirdPerson() : 0;
}

bool installPortalHooks() {
  void *renderPortals = nullptr;
  void *resetAll = nullptr;
  if (!resolve(abc::g_dwClientBase, "ClientPortalManager_RenderPortals",
               MH_GAMESYMBOL_KIND_FUNCTION, &renderPortals) ||
      !resolve(abc::g_dwClientBase, "ClientPortalManager_ResetAll",
               MH_GAMESYMBOL_KIND_FUNCTION, &resetAll))
    return false;
  const auto thirdPerson = GetProcAddress(
      static_cast<HMODULE>(abc::g_dwClientBase), "CL_IsThirdPerson");
  if (!thirdPerson)
    return false;
  RenderPortalsHook = g_pMetaHookAPI->InlineHook(
      renderPortals, reinterpret_cast<void *>(&portalRenderPortalsHook),
      reinterpret_cast<void **>(&OriginalRenderPortals));
  ResetAllHook = g_pMetaHookAPI->InlineHook(
      resetAll, reinterpret_cast<void *>(&portalResetAllHook),
      reinterpret_cast<void **>(&OriginalResetAll));
  ThirdPersonHook = g_pMetaHookAPI->InlineHook(
      reinterpret_cast<void *>(thirdPerson),
      reinterpret_cast<void *>(&thirdPersonHook),
      reinterpret_cast<void **>(&OriginalThirdPerson));
  if (!RenderPortalsHook || !ResetAllHook || !ThirdPersonHook) {
    uninstallPortalHooks();
    return false;
  }
  return true;
}

// ------------------------------------------------------------ drawfuncs ---

struct StudioLightBridge {
  hook_t *Hook = nullptr;
  void (*Target)(alight_t *) = nullptr;
  void (*Original)(alight_t *) = nullptr;
  const StudioLighting *Active = nullptr;
};

StudioLightBridge &studioLightBridge() {
  static StudioLightBridge bridge;
  return bridge;
}

void studioSetupLighting(alight_t *light) {
  auto &bridge = studioLightBridge();
  if (!bridge.Original)
    return;
  if (!bridge.Active || !light) {
    bridge.Original(light);
    return;
  }
  const auto &config = *bridge.Active;
  float direction[3];
  const float length = std::sqrt(config.Direction[0] * config.Direction[0] +
                                 config.Direction[1] * config.Direction[1] +
                                 config.Direction[2] * config.Direction[2]);
  for (int i = 0; i < 3; ++i)
    direction[i] =
        length > 0 ? config.Direction[i] / length : (i == 0 ? -1.0f : 0.0f);
  alight_t preview = *light;
  preview.ambientlight = config.Enabled ? config.Ambient : 255;
  preview.shadelight = config.Enabled ? config.Shade : 0;
  for (int i = 0; i < 3; ++i)
    preview.color[i] = config.Enabled ? config.Color[i] : 1.0f;
  preview.plightvec = direction;
  // Override at the final lighting seam, after dynamic/entity lighting has
  // run. StudioSetupLighting consumes these values synchronously.
  bridge.Original(&preview);
}

void installStudioLighting(engine_studio_api_s *api) {
  auto &bridge = studioLightBridge();
  bridge.Active = nullptr;
  // This API function belongs to the engine, not the reloadable client DLL.
  // Retain its hook across HUD cycles and client interface recaptures.
  if (!api || !api->StudioSetupLighting ||
      bridge.Target == api->StudioSetupLighting)
    return;
  if (bridge.Hook)
    g_pMetaHookAPI->UnHook(bridge.Hook);
  bridge = {};
  bridge.Target = api->StudioSetupLighting;
  // The client caches a COPY of the Studio API table. Hook the API function
  // itself rather than rewriting a table entry that the client no longer reads.
  bridge.Hook =
      g_pMetaHookAPI->InlineHook(reinterpret_cast<void *>(bridge.Target),
                                 reinterpret_cast<void *>(&studioSetupLighting),
                                 reinterpret_cast<void **>(&bridge.Original));
  if (!bridge.Hook) {
    bridge.Target = nullptr;
    abc::ConsoleWarn("[AbcUi model] custom Studio lighting unavailable; "
                     "using engine lighting\n");
  }
}

class StudioLightingScope {
public:
  explicit StudioLightingScope(const StudioLighting *lighting)
      : Saved(studioLightBridge().Active) {
    studioLightBridge().Active = lighting;
  }
  ~StudioLightingScope() {
    studioLightBridge().Active = Saved;
  }
  StudioLightingScope(const StudioLightingScope &) = delete;
  StudioLightingScope &operator=(const StudioLightingScope &) = delete;

private:
  const StudioLighting *Saved;
};

// Sets currententity and r_blend the way the engine's entity lists do.
class EntityDrawScope {
public:
  explicit EntityDrawScope(cl_entity_s *ent)
      : savedEntity(*Engine.currententity), savedBlend(*Engine.r_blend) {
    *Engine.currententity = ent;
    *Engine.r_blend = ent->curstate.rendermode == kRenderNormal
                          ? 1.0f
                          : Engine.CL_FxBlend(ent) / 255.0f;
  }
  ~EntityDrawScope() {
    *Engine.currententity = savedEntity;
    *Engine.r_blend = savedBlend;
  }
  EntityDrawScope(const EntityDrawScope &) = delete;
  EntityDrawScope &operator=(const EntityDrawScope &) = delete;

  bool visible() const {
    return *Engine.r_blend > 0.0f;
  }

private:
  cl_entity_s *savedEntity;
  float savedBlend;
};

bool studioModelPass(cl_entity_s *ent, cl_entity_s *boneSource,
                     const StudioLighting *lighting, int flags) {
  if (!ent || !ent->model || !StudioInterface || !*StudioInterface ||
      !(*StudioInterface)->StudioDrawModel)
    return false;
  if (boneSource) {
    if (!boneSource->model)
      return false;
    EntityDrawScope sourceScope(boneSource);
    // StudioMergeBones consumes the LAST StudioSaveBones result, not aiment.
    // Re-prime it for each follower: drawing one follower replaces that cache.
    // flags=0 also bypasses bbox culling for animation-only source models.
    if (!(*StudioInterface)->StudioDrawModel(0))
      return false;
  }
  EntityDrawScope scope(ent);
  if ((flags & STUDIO_RENDER) && !scope.visible())
    return false;
  StudioLightingScope lightingScope(lighting);
  // Only STUDIO_RENDER or 0, never STUDIO_EVENTS: a preview must not fire
  // sounds, muzzle flashes or attachment writes.
  return (*StudioInterface)->StudioDrawModel(flags) != 0;
}

class StudioDrawfuncs final : public Drawfuncs {
public:
  bool setupStudioPose(cl_entity_s *ent, cl_entity_s *boneSource) override;
  void drawStudioModel(cl_entity_s *ent, cl_entity_s *boneSource,
                       const StudioLighting *lighting) override;
};

StudioDrawfuncs DrawfuncsInstance;

bool StudioDrawfuncs::setupStudioPose(cl_entity_s *ent,
                                      cl_entity_s *boneSource) {
  return studioModelPass(ent, boneSource, nullptr, 0);
}

void StudioDrawfuncs::drawStudioModel(cl_entity_s *ent, cl_entity_s *boneSource,
                                      const StudioLighting *lighting) {
  studioModelPass(ent, boneSource, lighting, STUDIO_RENDER);
}

// ------------------------------------------------------------- novis ------

// The manager's empty world carries no BSP visibility data, so with the
// default vis path the client's StudioDrawModel culls the preview entity
// and the pass draws nothing. Ride the engine's no-visibility path for the
// duration of a view's pass; the scope arms only while the manager's empty
// world is actually installed, so on a real map the main view sees the
// player's own r_novis.
class NoVisScope {
public:
  NoVisScope()
      : Cvar(emptyWorldInstalled() ? abc::CVAR_GET_POINTER("r_novis")
                                   : nullptr) {
    if (!Cvar)
      return;
    Saved = Cvar->value;
    Cvar->value = 1.0f;
  }
  ~NoVisScope() {
    if (Cvar)
      Cvar->value = Saved;
  }
  NoVisScope(const NoVisScope &) = delete;
  NoVisScope &operator=(const NoVisScope &) = delete;

private:
  static bool emptyWorldInstalled() {
    return Engine.worldModel && *Engine.worldModel == sv::GetEmptyWorldModel();
  }

  cvar_t *Cvar = nullptr;
  float Saved = 0.0f;
};

// -------------------------------------------------------------- views -----

// One facade view registered with the manager. The wrapper adapts the
// facade surface to IStandaloneView and swaps the manager's
// IStandaloneViewDrawfuncs for the studio bridge above: the preview draws
// entities that need a private bone source and a lighting override, which
// the upstream interface does not model.
class ViewWrapper final : public IStandaloneView {
public:
  explicit ViewWrapper(View *facade) : Facade(facade) {}

  bool IsActive() const override {
    // The pass draws through the studio bridge; without the interface the
    // manager would run a pass that only captures a cleared viewport (the
    // per-frame gate the route always rendered under).
    return StudioInterface && *StudioInterface && Facade->isActive();
  }
  void CalcRefdef(ref_params_s *pparams) override {
    Facade->calcRefdef(pparams);
  }
  void OnBeforeRenderView() override {
    Facade->onBeforeRenderView();
    NoVis.emplace();
  }
  void OnAfterRenderView() override {
    NoVis.reset();
    Facade->onAfterRenderView();
  }
  // The manager's drawfuncs pointer is ignored (unnamed on purpose): the
  // facade views draw through DrawfuncsInstance only.
  void DrawNormalTriangles(IStandaloneViewDrawfuncs *) override {
    Facade->drawNormalTriangles(&DrawfuncsInstance);
  }
  void DrawTransparentTriangles(IStandaloneViewDrawfuncs *) override {
    Facade->drawTransparentTriangles(&DrawfuncsInstance);
  }

  View *facade() const {
    return Facade;
  }

private:
  View *Facade;
  std::optional<NoVisScope> NoVis;
};

std::vector<std::unique_ptr<ViewWrapper>> &wrappers() {
  static std::vector<std::unique_ptr<ViewWrapper>> instance;
  return instance;
}

auto findWrapper(View *view) {
  return std::ranges::find_if(wrappers(), [view](const auto &wrapper) {
    return wrapper->facade() == view;
  });
}

} // namespace

// ---------------------------------------------------------------- public --

void registerView(View *view) {
  if (!view || findWrapper(view) != wrappers().end())
    return;
  wrappers().push_back(std::make_unique<ViewWrapper>(view));
  sv::StandaloneViewManager().RegisterStandaloneView(wrappers().back().get());
}

void unregisterView(View *view) {
  const auto it = findWrapper(view);
  if (it == wrappers().end())
    return;
  // The list is in use until the views have rendered; unregistering from a
  // callback is a contract violation - the manager refuses and this keeps
  // the wrapper alive (the view is destroyed only outside callbacks, per
  // the interface contract).
  if (sv::StandaloneViewManager().IsRenderingStandaloneViews()) {
    abc::ConsoleWarn("[AbcUi] standalone view unregistered from inside its "
                     "own callback; ignored\n");
    return;
  }
  sv::StandaloneViewManager().UnregisterStandaloneView(it->get());
  wrappers().erase(it);
}

bool available() {
  return Available && ClientHooksReady;
}

void installEngineHooks() {
  if (EngineHooksInstalled)
    return;
  EngineHooksInstalled = true;
  Available = false;

  if (!g_pMetaHookAPI || !abc::g_dwEngineBase) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "module base missing); model preview disabled\n");
    return;
  }

  void *renderView = nullptr;
  void *paintBackground = nullptr;
  EngineBridge engine{};
  if (!resolve(abc::g_dwEngineBase, "V_RenderView", MH_GAMESYMBOL_KIND_FUNCTION,
               &renderView) ||
      !resolve(abc::g_dwEngineBase, "VGui_ViewportPaintBackground",
               MH_GAMESYMBOL_KIND_FUNCTION, &paintBackground) ||
      !resolve(abc::g_dwEngineBase, "cls.state", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.clientState)) ||
      !resolve(abc::g_dwEngineBase, "cls.signon", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.clientSignon)) ||
      !resolve(abc::g_dwEngineBase, "cl_worldmodel", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.worldModel)) ||
      !resolve(abc::g_dwEngineBase, "r_worldentity", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.worldEntity)) ||
      !resolve(abc::g_dwEngineBase, "CL_FxBlend", MH_GAMESYMBOL_KIND_FUNCTION,
               reinterpret_cast<void **>(&engine.CL_FxBlend)) ||
      !resolve(abc::g_dwEngineBase, "currententity", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.currententity)) ||
      !resolve(abc::g_dwEngineBase, "r_blend", MH_GAMESYMBOL_KIND_GLOBAL,
               reinterpret_cast<void **>(&engine.r_blend)) ||
      !renderView || !paintBackground) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "gamedata symbols unresolved on this build); model "
                     "preview disabled\n");
    return;
  }

  RenderViewHook = g_pMetaHookAPI->InlineHook(
      renderView, reinterpret_cast<void *>(&RenderViewHookFn),
      reinterpret_cast<void **>(&OriginalRenderView));
  PaintBackgroundHook = g_pMetaHookAPI->InlineHook(
      paintBackground, reinterpret_cast<void *>(&PaintBackgroundHookFn),
      reinterpret_cast<void **>(&OriginalPaintBackground));
  if (!RenderViewHook || !PaintBackgroundHook) {
    if (PaintBackgroundHook) {
      g_pMetaHookAPI->UnHook(PaintBackgroundHook);
      PaintBackgroundHook = nullptr;
    }
    if (RenderViewHook) {
      g_pMetaHookAPI->UnHook(RenderViewHook);
      RenderViewHook = nullptr;
    }
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (engine "
                     "hook install failed); model preview disabled\n");
    return;
  }

  Engine = engine;
  sv::StandaloneViewManager().SetEngine(makeManagerBridge());
  Available = true;
}

void installClientHooks() {
  if (!Available || ClientHooksReady)
    return;
  // The portal hooks are required on a Sven client (see installPortalHooks)
  // and the whole route degrades without them.
  if (isSvenCoopClient() && !installPortalHooks()) {
    abc::ConsoleWarn("[AbcUi] standalone view route unavailable (client "
                     "portal gamedata symbols unresolved); model preview "
                     "disabled\n");
    return;
  }
  ClientHooksReady = true;
}

void uninstallClientHooks() {
  // HUD_Shutdown also runs without unloading the client. Keep its studio
  // slot/API until the next real LoadClient; HUD_Init re-arms portal hooks.
  ClientHooksReady = false;
  uninstallPortalHooks();
}

bool hookCalcRefdef(ref_params_s *pparams) {
  if (!sv::StandaloneViewManager().GetCurrentView())
    return false;
  // The manager runs the current view's CalcRefdef and forces onlyClientDraw
  // / nextView; the client's V_CalcRefdef is skipped - its bob, punch and
  // pitch drift state advances per call and belongs to the main view only.
  sv::StandaloneViewManager().CalcRefdef(pparams);
  return true;
}

bool hookDrawNormalTriangles() {
  if (!sv::StandaloneViewManager().GetCurrentView())
    return false;
  sv::StandaloneViewManager().DrawNormalTriangles();
  return true;
}

bool hookDrawTransparentTriangles() {
  if (!sv::StandaloneViewManager().GetCurrentView())
    return false;
  sv::StandaloneViewManager().DrawTransparentTriangles();
  return true;
}

void captureStudioInterfaces(r_studio_interface_s **ppinterface,
                             engine_studio_api_s *pstudio) {
  // Keep the engine-owned slot, not its current value: when the client
  // declines the interface, the engine installs its fallback after return.
  StudioInterface = ppinterface;
  EngineStudio = pstudio;
  installStudioLighting(pstudio);
}

} // namespace standaloneview
} // namespace framework
