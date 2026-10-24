// modelpreview::Target - see Target.h.
//
// Since issue #24 the preview renders through the in-house engine
// V_RenderView route (framework/standaloneview, copied from
// MetaHookSv/StandaloneView) instead of Renderer's IEntityViewRenderer: the
// pass draws the model in the view's triangle callback, the engine owns the
// 3D matrices, and the result is captured from the viewport region into a
// backend offscreen target (clear + copy-out). Lighting follows the
// engine's own entity lighting - with the empty world installed at the
// menu, studio lighting is fullbright, so the card's light switches only
// affect the entity render state, not a per-view light.
#include <metahook.h>
#include "Target.h"

#include <com_model.h>
#include <const.h>
#include <r_studioint.h>
#include <studio.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Plugin.h>

#include "../../render/BackendHost.h"
#include "../../render/IRenderBackend.h"
#include "../../hooks.h"
#include "../../render/StandaloneView.h"
#include "../AbcRenderInterface.h"
#include "../RmlUiModule.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#undef min
#undef max

namespace modelpreview {
namespace {
std::vector<Target *> &targets() {
  static std::vector<Target *> Targets;
  return Targets;
}

bool RouteWarned = false;

abc::render::IOffscreenTargets *captureTargets() {
  return abc::render::targets();
}

bool routeAvailable() {
  if (framework::standaloneview::available() && captureTargets())
    return true;
  if (!RouteWarned) {
    RouteWarned = true;
    abc::ConsoleWarn("[AbcUi model] preview disabled (no engine view route "
                     "on this build, or the backend lacks offscreen "
                     "targets)\n");
  }
  return false;
}
} // namespace

// The view participant: options are synced once per framework frame by
// render() (the beat AFTER the engine's view pass), the pass itself runs
// inside V_RenderView before the main view.
struct Target::State : framework::standaloneview::View {
  cl_entity_t Entity{};
  std::string Source;
  std::string Uri;
  unsigned int CaptureHandle = 0;
  unsigned int CaptureTexture = 0;
  int CaptureWidth = 0;
  int CaptureHeight = 0;
  bool TargetRegistered = false;
  bool Attempted = false;
  bool Warned = false;
  bool ModelMissing = false;
  bool LastPassOk = false;
  unsigned int Frames = 0;
  bool Active = false;
  double LastRenderCall = 0;
  double LastTime = 0;
  float AnimationFrame = 0;
  int LastSequence = -1;
  float LastFrame = -1;
  // Camera resolved at sync time (auto-framing needs the model header).
  float ViewOrigin[3] = {0, 0, 0};
  float ViewAngles[3] = {0, 0, 0};
  float Background[4] = {0, 0, 0, 0};
  bool TransparentPass = false;

  bool isActive() const override {
    return Active && !ModelMissing && CaptureHandle != 0 &&
           gEngfuncs.GetAbsoluteTime() - LastRenderCall < 1.0;
  }

  void calcRefdef(struct ref_params_s *pparams) override {
    // Only vieworg / viewangles / viewport: the cl_* fields belong to the
    // main view (the engine writes them back). The engine keeps its own
    // projection fov (ref_params has no fov field on this engine
    // generation); the auto-framing math reads the engine's scr_fov_value
    // instead of the card's fov attribute.
    std::memcpy(pparams->vieworg, ViewOrigin, sizeof(ViewOrigin));
    std::memcpy(pparams->viewangles, ViewAngles, sizeof(ViewAngles));
    pparams->viewport[0] = 0;
    pparams->viewport[1] = 0;
    pparams->viewport[2] = CaptureWidth;
    pparams->viewport[3] = CaptureHeight;
  }

  void onBeforeRenderView() override {}

  void drawPass(framework::standaloneview::Drawfuncs *drawfuncs) {
    // The background clear must land inside the pass, on the framebuffer
    // the engine bound for this view - R_RenderView clears only depth (and
    // color just with gl_clear), so the region is cleared here before the
    // model draws, exactly over this view's viewport.
    if (abc::render::IOffscreenTargets *targets = captureTargets())
      targets->clearViewportRegion(0, 0, CaptureWidth, CaptureHeight,
                                   Background);
    drawfuncs->drawStudioModel(&Entity);
  }

  void drawNormalTriangles(
      framework::standaloneview::Drawfuncs *drawfuncs) override {
    if (!TransparentPass)
      drawPass(drawfuncs);
  }
  void drawTransparentTriangles(
      framework::standaloneview::Drawfuncs *drawfuncs) override {
    if (TransparentPass)
      drawPass(drawfuncs);
  }

  void onAfterRenderView() override {
    // Copy the viewport region out of the currently bound framebuffer into
    // the capture texture (StandaloneView's documented capture contract).
    abc::render::IOffscreenTargets *targets = captureTargets();
    if (!targets || !CaptureHandle)
      return;
    targets->copyViewportToTarget(CaptureHandle, 0, 0, CaptureWidth,
                                  CaptureHeight);
    const unsigned int texture = targets->texture(CaptureHandle);
    if (!texture)
      return;
    CaptureTexture = texture;
    LastPassOk = true;
    ++Frames;
    if (auto *renderInterface = rmlui::renderInterface();
        renderInterface && !TargetRegistered) {
      renderInterface->registerExternalTexture(
          Uri, CaptureTexture, CaptureWidth, CaptureHeight, nullptr,
          rmlui::TextureBlend::PremultipliedAlpha);
      TargetRegistered = true;
    }
  }

  void releaseCapture() {
    if (auto *renderInterface = rmlui::renderInterface();
        renderInterface && TargetRegistered)
      renderInterface->unregisterExternalTexture(Uri);
    TargetRegistered = false;
    CaptureTexture = 0;
    if (CaptureHandle && captureTargets())
      captureTargets()->free(CaptureHandle);
    CaptureHandle = 0;
    LastPassOk = false;
  }
};

Target::Target() : Data(std::make_unique<State>()) {
  static unsigned long long NextId = 0;
  Data->Uri = "modelfbo://" + std::to_string(++NextId) + "/";
  targets().push_back(this);
  framework::standaloneview::registerView(Data.get());
}

Target::~Target() {
  framework::standaloneview::unregisterView(Data.get());
  invalidate();
  std::erase(targets(), this);
}

void Target::invalidate() {
  Data->releaseCapture();
  Data->Entity = {};
  Data->Attempted = false;
  Data->ModelMissing = false;
  Data->LastSequence = -1;
  Data->LastTime = 0;
}

bool Target::render(const Options &Config, int Width, int Height) {
  State &state = *Data;
  if (!routeAvailable()) {
    state.Active = false;
    return false;
  }
  if (state.Source != Config.Source) {
    invalidate();
    state.Source = Config.Source;
  }
  if (Config.Source.empty() || Width <= 0 || Height <= 0) {
    state.Active = false;
    return false;
  }
  state.Active = true;
  state.LastRenderCall = gEngfuncs.GetAbsoluteTime();
  Width = std::clamp(Width, 1, 2048);
  Height = std::clamp(Height, 1, 2048);

  // Model loading through the engine studio API (mounted search paths
  // included); the old Renderer LoadStudioModel scope is gone.
  if (!state.Attempted) {
    state.Attempted = true;
    model_s *model = nullptr;
    if (hooks::EngineStudioApi && hooks::EngineStudioApi->Mod_ForName)
      model = hooks::EngineStudioApi->Mod_ForName(Config.Source.c_str(), 0);
    state.Entity.model = model;
    state.ModelMissing = !model;
    if (!model) {
      abc::ConsoleWarn("[AbcUi model] cannot load Studio model: %s\n",
                       Config.Source.c_str());
      return false;
    }
  }
  if (state.ModelMissing || !state.Entity.model)
    return false;

  // The capture texture rides the backend's offscreen targets; a size
  // change re-creates it (the handle changes, the URI registration rides
  // onAfterRenderView).
  if (abc::render::IOffscreenTargets *targets = captureTargets()) {
    if (state.CaptureHandle &&
        (state.CaptureWidth != Width || state.CaptureHeight != Height))
      state.releaseCapture();
    if (!state.CaptureHandle) {
      state.CaptureHandle = targets->create(Width, Height);
      state.CaptureWidth = Width;
      state.CaptureHeight = Height;
      if (!state.CaptureHandle)
        return false;
    }
  } else {
    return false;
  }

  auto &Entity = state.Entity;
  auto &EntityState = Entity.curstate;
  EntityState.rendermode = Config.RenderMode;
  EntityState.renderfx = Config.RenderFx;
  EntityState.renderamt = Config.RenderAmount;
  EntityState.rendercolor = {static_cast<byte>(Config.RenderColor[0]),
                             static_cast<byte>(Config.RenderColor[1]),
                             static_cast<byte>(Config.RenderColor[2])};
  EntityState.sequence =
      static_cast<short>(std::clamp(Config.Sequence, 0, 32767));
  EntityState.skin = static_cast<short>(std::clamp(Config.Skin, 0, 32767));
  EntityState.body = Config.Body;
  EntityState.scale = Config.Scale;
  EntityState.colormap = Config.ColorMap;
  Entity.mouth.mouthopen = static_cast<byte>(Config.Mouth);
  for (int i = 0; i < 4; i++) {
    EntityState.controller[i] = static_cast<byte>(Config.Controllers[i]);
    Entity.latched.prevcontroller[i] = EntityState.controller[i];
  }
  for (int i = 0; i < 2; i++) {
    EntityState.blending[i] = static_cast<byte>(Config.Blending[i]);
    Entity.latched.prevblending[i] = EntityState.blending[i];
  }
  EntityState.framerate =
      0; // Animation advances on UI time below, even without a server.
  std::copy(Config.Origin.begin(), Config.Origin.end(), Entity.origin);
  std::copy(Config.Origin.begin(), Config.Origin.end(), EntityState.origin);
  std::copy(Config.Angles.begin(), Config.Angles.end(), Entity.angles);
  std::copy(Config.Angles.begin(), Config.Angles.end(), EntityState.angles);
  state.TransparentPass = Config.RenderMode != kRenderNormal;

  const double time = gEngfuncs.GetAbsoluteTime();
  if (state.LastSequence != Config.Sequence ||
      state.LastFrame != Config.Frame) {
    state.AnimationFrame = Config.Frame;
    state.LastSequence = Config.Sequence;
    state.LastFrame = Config.Frame;
  }
  // Use the model's own sequence table (bounds-checked against the header
  // length: seqindex comes from the untrusted .mdl file).
  auto *header = static_cast<studiohdr_t *>(state.Entity.model->cache.data);
  if (!header || header->numseq <= 0 || header->length <= 0)
    return false;
  const auto headerSize = static_cast<std::size_t>(header->length);
  const auto seqOffset = static_cast<std::size_t>(header->seqindex);
  const auto seqBytes =
      static_cast<std::size_t>(header->numseq) * sizeof(mstudioseqdesc_t);
  if (seqOffset > headerSize || seqBytes > headerSize - seqOffset)
    return false;
  EntityState.sequence =
      static_cast<short>(std::clamp(Config.Sequence, 0, header->numseq - 1));
  EntityState.skin = static_cast<short>(
      std::clamp(Config.Skin, 0, std::max(header->numskinfamilies - 1, 0)));
  const auto *sequence =
      reinterpret_cast<const mstudioseqdesc_t *>(
          reinterpret_cast<const byte *>(header) + header->seqindex) +
      EntityState.sequence;
  // The .mdl sequence descriptor is asset data: a garbage fps (0, 1e30,
  // NaN) must not stop the animation or push inf/NaN into the render state.
  const float sequenceFps = std::isfinite(sequence->fps)
                                ? std::clamp(sequence->fps, 0.0f, 240.0f)
                                : 0.0f;
  if (Config.Animate && state.LastTime > 0 && sequence->numframes > 1 &&
      sequenceFps > 0.0f) {
    const double delta = std::clamp(time - state.LastTime, 0.0, 0.1);
    state.AnimationFrame += static_cast<float>(delta) * Config.FrameRate *
                            sequenceFps * 256.0f / (sequence->numframes - 1);
    state.AnimationFrame = std::fmod(state.AnimationFrame, 256.0f);
    if (state.AnimationFrame < 0)
      state.AnimationFrame += 256;
  }
  state.LastTime = time;
  EntityState.frame = state.AnimationFrame;
  EntityState.animtime = static_cast<float>(gEngfuncs.GetClientTime());
  Entity.latched.prevanimtime = EntityState.animtime;
  Entity.latched.prevframe = EntityState.frame;
  Entity.latched.prevsequence = EntityState.sequence;

  // Camera + background resolved at sync time (auto-framing reads the
  // sequence bounding box from the header).
  std::copy(Config.View.Origin.begin(), Config.View.Origin.end(),
            state.ViewOrigin);
  std::copy(Config.View.Angles.begin(), Config.View.Angles.end(),
            state.ViewAngles);
  std::copy(Config.View.Background.begin(), Config.View.Background.end(),
            state.Background);
  if (Config.AutoFrame) {
    float top = sequence->bbmax[2];
    float bottom = sequence->bbmin[2];
    if (!(top > bottom)) {
      top = header->bbmax[2];
      bottom = header->bbmin[2];
    }
    if (top > bottom) {
      // The engine owns the projection fov, so frame for the engine's own
      // horizontal fov (scr_fov_value, default 90) converted to vertical
      // by the target aspect; the card's fov attribute only advises.
      float engineFov = 90.0f;
      if (cvar_t *fov = abc::CVAR_GET_POINTER("scr_fov_value"))
        engineFov = std::clamp(fov->value, 30.0f, 170.0f);
      const float aspect =
          Height > 0 ? static_cast<float>(Width) / Height : 1.0f;
      const float halfRadians =
          2.0f *
          std::atan(std::tan(engineFov * 0.5f * 3.14159265358979f / 180.0f) /
                    aspect);
      const float modelHeight = (top - bottom) * Config.FrameMargin;
      const float center = (top + bottom) * 0.5f;
      const float distance =
          std::max(modelHeight * 0.5f / std::tan(halfRadians), 8.0f);
      state.ViewOrigin[0] = distance;
      state.ViewOrigin[1] = 0.0f;
      state.ViewOrigin[2] = center;
      state.ViewAngles[0] = 0.0f;
      state.ViewAngles[1] = 180.0f;
      state.ViewAngles[2] = 0.0f;
    }
  }
  // Readiness follows the last pass; the pass runs one engine frame later
  // (V_RenderView precedes the end-frame beat that syncs options).
  return state.LastPassOk && state.CaptureTexture != 0;
}

const std::string &Target::uri() const {
  return Data->Uri;
}
unsigned int Target::texture() const {
  return Data->CaptureTexture;
}
unsigned int Target::frames() const {
  return Data->Frames;
}
bool hasWorld() {
  return framework::standaloneview::available() && gEngfuncs.pfnGetLevelName &&
         gEngfuncs.pfnGetLevelName()[0] != '\0';
}
void invalidateAll() {
  for (auto *Target : targets())
    Target->invalidate();
}
} // namespace modelpreview
