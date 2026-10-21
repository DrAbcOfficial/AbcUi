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
#include "../framework/ColorUtil.h"
#include "../framework/GlyphText.h"
#include "../framework/HudPanel.h"
#include "../framework/Localization.h"
#include "../framework/PanelManager.h"
#include "../framework/TextUtil.h"
#include "../rmlui/VGUI2ExtensionImport.h"

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
    sizeof(AbcUiUiApi),    apiPanelDocument,       apiDocumentShow,
    apiDocumentHide,       apiDocumentSetProperty, apiQuerySelector,
    apiElementSetProperty, apiElementSetInnerRML,  apiInkMarkup,
    apiTranslate,          apiEscapeRml,           apiKeyNameForKey,
    apiDocumentVisible,    apiElementAppendRml,    apiElementRemove,
    apiElementInnerRml,    apiFormatColor,         apiDifferColor,
};

} // namespace

const AbcUiUiApi *get() {
  return &kUiApi;
}

} // namespace UiApi

namespace DataApi {
namespace {

void apiHudState(AbcUiHudState *out) {
  if (!out)
    return;
  *out = AbcUiHudState{};
  out->structSize = sizeof(AbcUiHudState);
  out->suit = hudstate::hasSuit() ? 1 : 0;
  out->spectate = hudstate::isSpectate() ? 1 : 0;
  out->longjump = hudstate::hasLongJump() ? 1 : 0;
  out->hideBits = hudstate::debugHideTokens();
  const hudstate::MessageSnapshot &messages = hudstate::messages();
  out->flashOn = messages.FlashOn ? 1 : 0;
  out->flashBattery = messages.FlashBattery;
  out->health = static_cast<int32_t>(hudstate::health());
  out->armor = messages.Armor;
  out->waterLevel = hudstate::waterlevel();
  out->fov = hudstate::fov();
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
  *out = AbcUiPlayerState{};
  out->structSize = sizeof(AbcUiPlayerState);
  watchScoreInfo();
  out->frags = localFrags();
  const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
  if (!local)
    return;
  out->index = local->index;
  const float *velocity = local->curstate.velocity;
  out->speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
}

int apiLevelName(char *out, uint32_t outSize) {
  const char *name = gEngfuncs.pfnGetLevelName();
  const std::string text = name ? name : "";
  if (out && outSize > 0)
    copyOut(text, out, outSize);
  return static_cast<int>(text.size());
}

const AbcUiDataApi kDataApi = {
    sizeof(AbcUiDataApi),
    apiHudState,
    apiLevelName,
    apiPlayerState,
};

} // namespace

const AbcUiDataApi *get() {
  return &kDataApi;
}

} // namespace DataApi
} // namespace framework
