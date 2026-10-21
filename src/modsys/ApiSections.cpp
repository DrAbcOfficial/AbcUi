// framework::DataApi / framework::UiApi - sdk data and ui sections. See
// ApiTable.h for how they join the module table.
#include "ApiTable.h"

#include <cstring>
#include <string>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "../data/HudState.h"
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

int apiDocumentVisible(AbcUiDocument *doc) {
  const HudPanel *panel = panelOf(doc);
  return panel && panel->isVisible() ? 1 : 0;
}

const AbcUiUiApi kUiApi = {
    sizeof(AbcUiUiApi),    apiPanelDocument,       apiDocumentShow,
    apiDocumentHide,       apiDocumentSetProperty, apiQuerySelector,
    apiElementSetProperty, apiElementSetInnerRML,  apiInkMarkup,
    apiTranslate,          apiEscapeRml,           apiKeyNameForKey,
    apiDocumentVisible,
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
};

} // namespace

const AbcUiDataApi *get() {
  return &kDataApi;
}

} // namespace DataApi
} // namespace framework
