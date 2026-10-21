// framework::DataApi / framework::UiApi - sdk data and ui sections. See
// ApiTable.h for how they join the module table.
#include "ApiTable.h"

#include <cmath>
#include <cstring>
#include <string>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../data/HudState.h"
#include "../data/NetworkMessages.h"
#include "../data/PlayerTrace.h"
#include "../framework/ColorUtil.h"
#include "../framework/GlyphText.h"
#include "../framework/HudPanel.h"
#include "../framework/Localization.h"
#include "../framework/Projection.h"
#include "../framework/PanelManager.h"
#include "../framework/TextUtil.h"
#include "../framework/Theme.h"
#include "../rmlui/VGUI2ExtensionImport.h"
#include "../services/crosshair/Geometry.h"
#include "../rmlui/animated/ElementAnimatedImage.h"
#include "../rmlui/texture/SpriteResource.h"

namespace framework {
namespace {

// Copies a string into a module-owned buffer, always terminating; returns the
// number of bytes written (never outSize or more).
int copyOut(const std::string &text, char *out, uint32_t outSize) {
  if (!out || outSize == 0)
    return 0;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < text.size(); ++i)
    out[i] = text[i];
  out[i] = '\0';
  return static_cast<int>(i);
}

} // namespace

namespace UiApi {
namespace {

HudPanel *panelOf(AbcUiDocument *doc) {
  return reinterpret_cast<HudPanel *>(doc);
}

Rml::Element *elementOf(AbcUiElement *element) {
  return reinterpret_cast<Rml::Element *>(element);
}

AbcUiDocument *apiPanelDocument(const char *panelName) {
  if (!panelName)
    return nullptr;
  return reinterpret_cast<AbcUiDocument *>(PanelManager::find(panelName));
}

void apiDocumentShow(AbcUiDocument *doc) {
  if (HudPanel *panel = panelOf(doc))
    panel->show();
}

void apiDocumentHide(AbcUiDocument *doc) {
  if (HudPanel *panel = panelOf(doc))
    panel->hide();
}

void apiDocumentSetProperty(AbcUiDocument *doc, const char *name,
                            const char *value) {
  HudPanel *panel = panelOf(doc);
  if (!panel || !name || !value)
    return;
  if (Rml::ElementDocument *document = panel->getDocument())
    document->SetProperty(name, value);
}

AbcUiElement *apiQuerySelector(AbcUiDocument *doc, const char *id) {
  HudPanel *panel = panelOf(doc);
  if (!panel || !id || !*id)
    return nullptr;
  const char *key = (*id == '#') ? id + 1 : id;
  if (Rml::ElementDocument *document = panel->getDocument())
    return reinterpret_cast<AbcUiElement *>(document->GetElementById(key));
  return nullptr;
}

void apiElementSetProperty(AbcUiElement *element, const char *name,
                           const char *value) {
  if (element && name && value)
    elementOf(element)->SetProperty(name, value);
}

void apiElementSetInnerRML(AbcUiElement *element, const char *rml) {
  if (element && rml)
    elementOf(element)->SetInnerRML(rml);
}

int apiInkMarkup(const char *text, const char *tint, char *out,
                 uint32_t outSize) {
  if (!text || !out || outSize == 0)
    return 0;
  const std::string markup =
      glyphs::inkMarkup(text, tint && *tint ? tint : glyphs::kAccentTint);
  if (markup.empty())
    return 0;
  uint32_t i = 0;
  for (; i + 1 < outSize && i < markup.size(); ++i)
    out[i] = markup[i];
  out[i] = '\0';
  return 1;
}

int apiTranslate(const char *token, char *out, uint32_t outSize) {
  if (!token || !*token || !out || outSize == 0)
    return 0;
  std::string translated;
  if (!Localization::translate(translated, token))
    return 0;
  copyOut(translated, out, outSize);
  return 1;
}

int apiEscapeRml(const char *text, char *out, uint32_t outSize) {
  if (!text)
    return 0;
  const std::string escaped = TextUtil::escapeRml(text);
  if (out && outSize > 0)
    copyOut(escaped, out, outSize);
  return static_cast<int>(escaped.size());
}

int apiKeyNameForKey(int keynum, char *out, uint32_t outSize) {
  const char *name = vgui2ext::keyNameForKey(keynum);
  const std::string text = name ? name : "";
  if (out && outSize > 0)
    copyOut(text, out, outSize);
  return static_cast<int>(text.size());
}

void apiFormatColor(const float rgb[3], char *out, uint32_t outSize) {
  if (!rgb || !out || outSize == 0)
    return;
  copyOut(ColorUtil::format(rgb), out, outSize);
}

void apiDifferColor(const float base[3], const float danger[3], float ratio,
                    char *out, uint32_t outSize) {
  if (!base || !danger || !out || outSize == 0)
    return;
  float rgb[3];
  ColorUtil::differColor(base, danger, ratio, rgb);
  copyOut(ColorUtil::format(rgb), out, outSize);
}

void apiElementSetAttribute(AbcUiElement *element, const char *name,
                            const char *value) {
  if (!element || !name || !*name)
    return;
  Rml::Element *target = elementOf(element);
  if (!value)
    target->RemoveAttribute(name);
  else
    target->SetAttribute(name, value);
}

void apiElementSetClass(AbcUiElement *element, const char *name, int enabled) {
  if (!element || !name || !*name)
    return;
  elementOf(element)->SetClass(name, enabled != 0);
}

void apiElementRestartAnimation(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return;
  if (auto *animated =
          rmlui_dynamic_cast<rmlui::animated::ElementAnimatedImage *>(target))
    animated->Restart();
}

void apiFormatRgba(int r, int g, int b, int a, char *out, uint32_t outSize) {
  copyOut(ColorUtil::formatRgba(r, g, b, a), out, outSize);
}

int apiProject(const float world[3], float screen[2]) {
  if (!world || !screen)
    return 1;
  return projection::worldToScreen(world, screen);
}

int apiDocumentSize(AbcUiDocument *doc, int *width, int *height) {
  HudPanel *panel = panelOf(doc);
  Rml::ElementDocument *document = panel ? panel->getDocument() : nullptr;
  Rml::Context *context = document ? document->GetContext() : nullptr;
  if (!context)
    return 0;
  const Rml::Vector2i dimensions = context->GetDimensions();
  if (width)
    *width = dimensions.x;
  if (height)
    *height = dimensions.y;
  return 1;
}

float apiScale() {
  return Theme::scale();
}

void apiCrosshairLayout(const AbcUiCrosshairLayout *layout, float core[5][4],
                        float outline[5][4], int32_t visible[5]) {
  if (!layout)
    return;
  using services::crosshair::Part;
  services::crosshair::Layout request;
  request.Thickness = layout->thickness;
  request.Length = layout->length;
  request.Offset = layout->offset;
  request.OutlineWidth = layout->outlineWidth;
  request.Dot = layout->dot != 0;
  request.TShape = layout->tShape != 0;
  services::crosshair::Rect coreRects[services::crosshair::kPartCount];
  services::crosshair::Rect outlineRects[services::crosshair::kPartCount];
  services::crosshair::coreRects(request, coreRects);
  services::crosshair::outlineRects(request, outlineRects);
  static constexpr Part kParts[services::crosshair::kPartCount] = {
      Part::kCenter, Part::kTop, Part::kBottom, Part::kLeft, Part::kRight};
  for (int i = 0; i < services::crosshair::kPartCount; ++i) {
    if (core) {
      core[i][0] = coreRects[i].X;
      core[i][1] = coreRects[i].Y;
      core[i][2] = coreRects[i].Width;
      core[i][3] = coreRects[i].Height;
    }
    if (outline) {
      outline[i][0] = outlineRects[i].X;
      outline[i][1] = outlineRects[i].Y;
      outline[i][2] = outlineRects[i].Width;
      outline[i][3] = outlineRects[i].Height;
    }
    if (visible)
      visible[i] = services::crosshair::partVisible(request, kParts[i]) ? 1 : 0;
  }
}

int apiHudSprite(const char *name, char *atlas, uint32_t atlasSize,
                 int32_t rect[4]) {
  if (!name)
    return 0;
  std::string sprite;
  wrect_t crop{};
  if (!sprites::findHudSprite(name, sprite, crop))
    return 0;
  if (atlas && atlasSize > 0)
    copyOut(sprite, atlas, atlasSize);
  if (rect) {
    rect[0] = crop.left;
    rect[1] = crop.top;
    rect[2] = crop.right;
    rect[3] = crop.bottom;
  }
  return 1;
}

int apiDocumentVisible(AbcUiDocument *doc) {
  const HudPanel *panel = panelOf(doc);
  return panel && panel->isVisible() ? 1 : 0;
}

AbcUiElement *apiElementAppendRml(AbcUiElement *parent, const char *rml,
                                  const char *className) {
  Rml::Element *element = elementOf(parent);
  if (!element || !rml)
    return nullptr;
  Rml::ElementDocument *document = element->GetOwnerDocument();
  if (!document)
    return nullptr;
  Rml::ElementPtr holder = document->CreateElement("div");
  if (!holder)
    return nullptr;
  Rml::Element *created = holder.get();
  if (className && *className)
    created->SetClass(className, true);
  created->SetInnerRML(rml);
  element->AppendChild(std::move(holder));
  return reinterpret_cast<AbcUiElement *>(created);
}

void apiElementRemove(AbcUiElement *element) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return;
  if (Rml::Element *parent = target->GetParentNode())
    parent->RemoveChild(target);
}

int apiElementInnerRml(AbcUiElement *element, char *out, uint32_t outSize) {
  Rml::Element *target = elementOf(element);
  if (!target)
    return 0;
  const std::string inner = target->GetInnerRML();
  if (out && outSize > 0)
    copyOut(inner, out, outSize);
  return static_cast<int>(inner.size());
}

const AbcUiUiApi kUiApi = {
    sizeof(AbcUiUiApi),
    apiPanelDocument,
    apiDocumentShow,
    apiDocumentHide,
    apiDocumentSetProperty,
    apiQuerySelector,
    apiElementSetProperty,
    apiElementSetInnerRML,
    apiInkMarkup,
    apiTranslate,
    apiEscapeRml,
    apiKeyNameForKey,
    apiDocumentVisible,
    apiElementAppendRml,
    apiElementRemove,
    apiElementInnerRml,
    apiFormatColor,
    apiDifferColor,
    apiElementSetAttribute,
    apiElementSetClass,
    apiHudSprite,
    apiElementRestartAnimation,
    apiFormatRgba,
    apiProject,
    apiDocumentSize,
    apiCrosshairLayout,
    apiScale,
};

} // namespace

const AbcUiUiApi *get() {
  return &kUiApi;
}

} // namespace UiApi

namespace DataApi {
namespace {

// Snapshot writers honor the caller's declared structSize (ABI rule: a
// caller may be built against an older, shorter header). 0 means "the
// framework's current size".
size_t declaredBytes(size_t declared, size_t full) {
  return (declared == 0 || declared > full) ? full : declared;
}

void apiHudState(AbcUiHudState *out) {
  if (!out)
    return;
  const size_t bytes = declaredBytes(out->structSize, sizeof(AbcUiHudState));
  AbcUiHudState state{};
  AbcUiHudState *target = &state;
  state.structSize = sizeof(AbcUiHudState);
  target->suit = hudstate::hasSuit() ? 1 : 0;
  target->spectate = hudstate::isSpectate() ? 1 : 0;
  target->longjump = hudstate::hasLongJump() ? 1 : 0;
  target->hideBits = hudstate::debugHideTokens();
  const hudstate::MessageSnapshot &messages = hudstate::messages();
  target->flashOn = messages.FlashOn ? 1 : 0;
  target->flashBattery = messages.FlashBattery;
  target->health = static_cast<int32_t>(hudstate::health());
  target->armor = messages.Armor;
  target->waterLevel = hudstate::waterlevel();
  target->fov = hudstate::fov();
  const float *punch = hudstate::punchangle();
  if (punch) {
    target->punchAngle[0] = punch[0];
    target->punchAngle[1] = punch[1];
    target->punchAngle[2] = punch[2];
  }
  std::memcpy(out, &state, bytes);
}

// The ScoreInfo feed carries the scoreboard rows; the local row's frags is
// what the HUD score readouts show (old panel-local latch).
int &localFrags() {
  static int frags = 0;
  return frags;
}

void onScoreInfo(int clientIndex, float frags, int, float, float, int, int,
                 int) {
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (local && clientIndex == local->index)
    localFrags() = static_cast<int>(frags);
}

void watchScoreInfo() {
  static bool watched = false;
  if (watched)
    return;
  watched = true;
  netmsg::ScoreInfo.append(&onScoreInfo);
}

void apiPlayerState(AbcUiPlayerState *out) {
  if (!out)
    return;
  const size_t bytes = declaredBytes(out->structSize, sizeof(AbcUiPlayerState));
  AbcUiPlayerState state{};
  state.structSize = sizeof(AbcUiPlayerState);
  watchScoreInfo();
  state.frags = localFrags();
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (local) {
    state.index = local->index;
    const float *velocity = local->curstate.velocity;
    state.speed =
        std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    state.origin[0] = local->curstate.origin[0];
    state.origin[1] = local->curstate.origin[1];
    state.origin[2] = local->curstate.origin[2];
    state.messageNum = local->curstate.messagenum;
    gEngfuncs.GetViewAngles(state.angles);
  }
  std::memcpy(out, &state, bytes);
}

int32_t apiThirdPerson() {
  return gExportfuncs.CL_IsThirdPerson() ? 1 : 0;
}

int32_t apiTraceEndPos(int32_t type, float out[3]) {
  if (!out)
    return 0;
  playertrace::TraceType traceType = playertrace::TraceType::View;
  switch (type) {
  case ABCUI_TRACE_HEAD:
    traceType = playertrace::TraceType::Head;
    break;
  case ABCUI_TRACE_FOOT:
    traceType = playertrace::TraceType::Foot;
    break;
  default:
    traceType = playertrace::TraceType::View;
    break;
  }
  const pmtrace_t *trace = playertrace::get(traceType);
  if (!trace)
    return 0;
  out[0] = trace->endpos[0];
  out[1] = trace->endpos[1];
  out[2] = trace->endpos[2];
  return 1;
}

int apiLevelName(char *out, uint32_t outSize) {
  const char *name = gEngfuncs.pfnGetLevelName();
  const std::string text = name ? name : "";
  if (out && outSize > 0)
    copyOut(text, out, outSize);
  return static_cast<int>(text.size());
}

const AbcUiDataApi kDataApi = {
    sizeof(AbcUiDataApi), apiHudState,    apiLevelName,
    apiPlayerState,       apiThirdPerson, apiTraceEndPos,
};

} // namespace

const AbcUiDataApi *get() {
  return &kDataApi;
}

} // namespace DataApi
} // namespace framework
