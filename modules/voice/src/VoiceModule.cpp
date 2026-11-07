// abcui.voice - the team-voice speaker list.
//
// Replaces the client's native voice-status labels (CVoiceStatus, fed by the
// engine's HUD_VoiceStatus export). The framework publishes every talk change
// on voice.status and lets this module claim ABCUI_NATIVE_HUD_VOICE, so the
// native labels are never created while the document is ready. Rows are the
// speaker's avatar plus name and enter from the right (.voice-row keyframe);
// the badge shows when the local client refuses to send or receive voice.
#include "../../common/Avatars.h"
#include "../../common/Panel.h"
#include "../../common/UiText.h"
#include <abcui/events.h>
#include <abcui/module_api.h>

#include <map>
#include <string>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiPanelDesc Panel = {};
bool DocumentReady = false;
bool ServerMuted = false;
int StatusBits = -1; // -1 forces the first badge pass
// Badge presentation: it opens with the state text, then retracts to the
// translucent icon after BadgeHoldSeconds (driven by the frame pump).
constexpr double BadgeHoldSeconds = 2.6;
double BadgeAge = 0.0;
bool BadgeCollapsed = false;
AbcUiCvarHandle MicCvar = nullptr;
AbcUiCvarHandle ReceiveCvar = nullptr;

struct Row {
  AbcUiElement *element = nullptr;
  uint64_t steamId = 0;
  std::string name;
};
std::map<int, Row> Rows;

constexpr const char *kPanelName = "voice";
constexpr const char *kDocumentPath = "mod://abcui.voice/hud/rml/voice.rml";
constexpr const char *kDefaultAvatar =
    "mod://abcui.voice/hud/img/default_avatar.apng";

AbcUiDocument *panelDocument() {
  return Api ? moduleui::panelDocument(Api, kPanelName) : nullptr;
}

AbcUiElement *element(const char *id) {
  AbcUiDocument *doc = panelDocument();
  return doc ? Api->ui->querySelector(doc, id) : nullptr;
}

std::string escape(const char *text) {
  return moduleui::escapeText(Api, text);
}

std::string escape(const std::string &text) {
  return moduleui::escapeText(Api, text);
}

std::string translateOr(const char *token, const char *fallback) {
  char buffer[128] = {};
  if (Api->ui->translate(token, buffer, sizeof(buffer)))
    return buffer;
  return fallback;
}

void setDisplay(const char *id, const char *value) {
  if (AbcUiElement *target = element(id))
    Api->ui->elementSetProperty(target, "display", value);
}

void setClass(const char *id, const char *name, bool on) {
  if (AbcUiElement *target = element(id))
    Api->ui->elementSetClass(target, name, on ? 1 : 0);
}

void setInner(const char *id, const std::string &rml) {
  if (AbcUiElement *target = element(id))
    Api->ui->elementSetInnerRML(target, rml.c_str());
}

// Rows can outlive a layout pass but never a context rebuild: the document
// callbacks clear the map without touching element handles (the context owns
// them). Only elementRemove drops a live handle.
void clearRows() {
  Rows.clear();
}

void setVisible(bool visible) {
  AbcUiDocument *doc = panelDocument();
  if (!doc)
    return;
  const bool isVisible = Api->ui->documentVisible(doc) != 0;
  if (visible && !isVisible)
    Api->ui->documentShow(doc);
  else if (!visible && isVisible)
    Api->ui->documentHide(doc);
}

void refreshVisibility() {
  // The badge alone is a reason to show: a muted client has no speakers but
  // still needs the status icon.
  setVisible(DocumentReady && (!Rows.empty() || StatusBits > 0));
}

std::string rowRml(const AbcUiPlayerInfo &info) {
  std::string avatar;
  const bool animated =
      moduleui::animatedAvatarUri(Api, info.steamId64, avatar);
  if (!animated)
    avatar = moduleui::avatarUri(Api, info.steamId64, kDefaultAvatar);
  const char *tag = animated ? "animimg" : "img";
  return std::string("<") + tag + " class='voice-avatar' src='" + avatar +
         "'/><span class='voice-name'>" + escape(info.name) + "</span>";
}

void addSpeaker(int index) {
  AbcUiPlayerInfo info{};
  info.structSize = sizeof(info);
  if (!Api->players->get(index, &info) || !info.valid)
    return;
  // A row that already shows this identity keeps its entrance animation; only
  // a rename or a Steam identity change rebuilds it.
  auto it = Rows.find(index);
  if (it != Rows.end() && it->second.steamId == info.steamId64 &&
      it->second.name == info.name)
    return;
  if (it != Rows.end()) {
    Api->ui->elementRemove(it->second.element);
    Rows.erase(it);
  }
  AbcUiElement *list = element("voice-list");
  if (!list)
    return;
  AbcUiElement *row =
      Api->ui->elementAppendRml(list, rowRml(info).c_str(), "voice-row");
  if (!row)
    return;
  Rows[index] = Row{row, info.steamId64, info.name};
  refreshVisibility();
}

void removeSpeaker(int index) {
  auto it = Rows.find(index);
  if (it == Rows.end())
    return;
  Api->ui->elementRemove(it->second.element);
  Rows.erase(it);
  refreshVisibility();
}

// 0 = voice available, bit0 = mic off, bit1 = receive off, bit2 = the engine
// reported the local voice ban. voice_modenable is the client's own mic
// toggle; voice_enable belongs to the engine and may be absent (treated as
// enabled).
void updateStatus() {
  if (!Api)
    return;
  if (!MicCvar)
    MicCvar = Api->console->findCvar("voice_modenable");
  if (!ReceiveCvar)
    ReceiveCvar = Api->console->findCvar("voice_enable");
  const bool micOn =
      !MicCvar || Api->console->cvarValue(MicCvar, 1.0f) > 0.5f;
  const bool receiveOn =
      !ReceiveCvar || Api->console->cvarValue(ReceiveCvar, 1.0f) > 0.5f;
  int bits = 0;
  if (!micOn)
    bits |= 1;
  if (!receiveOn)
    bits |= 2;
  if (ServerMuted)
    bits |= 4;
  if (bits == StatusBits)
    return;
  StatusBits = bits;

  const char *token = nullptr;
  const char *fallback = "";
  switch (bits) {
  case 1:
    token = "Voice_MicOff";
    fallback = "Mic off";
    break;
  case 2:
    token = "Voice_ReceiveOff";
    fallback = "Voice receive off";
    break;
  case 4:
    token = "Voice_Muted";
    fallback = "Voice muted";
    break;
  case 0:
    break;
  default:
    token = "Voice_Off";
    fallback = "Voice off";
    break;
  }
  setDisplay("voice-badge", bits ? "flex" : "none");
  // The mic glyph is only for a send-only mute. A receive mute (with or
  // without the mic) and the engine's ban read as "voice off": speaker glyph.
  const bool micOnly = (bits & 1) != 0 && (bits & 2) == 0 && (bits & 4) == 0;
  setDisplay("voice-icon-mic", micOnly ? "flex" : "none");
  setDisplay("voice-icon-speaker", micOnly ? "none" : "flex");
  if (token)
    setInner("voice-badge-text", escape(translateOr(token, fallback)));
  // A new state re-opens the badge (text visible) and restarts the hold.
  BadgeAge = 0.0;
  BadgeCollapsed = false;
  setClass("voice-badge", "collapsed", false);
  refreshVisibility();
}

// ---- panel callbacks ----

void panelDocumentReady(void *) {
  DocumentReady = true;
  StatusBits = -1;
  BadgeAge = 0.0;
  BadgeCollapsed = false;
  clearRows();
  updateStatus();
  refreshVisibility();
}

void panelOnData(void *, double frameTime) {
  // The mic/receive toggles are cvars, not events: poll once per frame. A
  // change only rewrites the badge when the state class actually moved.
  updateStatus();
  if (StatusBits <= 0 || BadgeCollapsed)
    return;
  if (frameTime > 0.0 && frameTime < 0.25)
    BadgeAge += frameTime;
  if (BadgeAge >= BadgeHoldSeconds) {
    BadgeCollapsed = true;
    setClass("voice-badge", "collapsed", true);
  }
}

void panelReset(void *) {
  // Map transition: the document survives, so live rows must be removed.
  for (auto &entry : Rows)
    if (entry.second.element)
      Api->ui->elementRemove(entry.second.element);
  Rows.clear();
  ServerMuted = false;
  StatusBits = -1;
  BadgeAge = 0.0;
  BadgeCollapsed = false;
  setVisible(false);
}

// ---- events ----

void onVoiceStatus(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiVoiceStatusEvent *>(payload);
  if (!event)
    return;
  if (event->index == -2) {
    ServerMuted = event->talking != 0;
    updateStatus();
    return;
  }
  if (!DocumentReady)
    return;
  if (event->index == -1) {
    // voice_loopback: the engine reports the local player's own talking
    // state, and the native status showed it under the same cvar.
    AbcUiCvarHandle loopback = Api->console->findCvar("voice_loopback");
    if (!loopback || Api->console->cvarValue(loopback, 0.0f) <= 0.5f)
      return;
    AbcUiPlayerInfo self{};
    self.structSize = sizeof(self);
    Api->players->local(&self);
    if (!self.valid)
      return;
    if (event->talking)
      addSpeaker(self.index);
    else
      removeSpeaker(self.index);
    return;
  }
  if (event->index < 1 || event->index > ABCUI_MAX_PLAYERS)
    return;
  if (event->talking)
    addSpeaker(event->index);
  else
    removeSpeaker(event->index);
}

int onNativeVoiceDraw(void *, const void *payload) {
  const auto *event = static_cast<const AbcUiNativeHudEvent *>(payload);
  if (!Api || !event || event->element != ABCUI_NATIVE_HUD_VOICE)
    return 0;
  // The module owns the voice surface for the whole session once its document
  // is ready; a hidden list (nobody talking) is still authoritative, so the
  // native labels must not reappear between talks.
  return DocumentReady ? 1 : 0;
}

// ---- lifecycle ----

void onModuleLoad() {
  Api->events->subscribe(ABCUI_CHANNEL_VOICE_STATUS, 0, &onVoiceStatus, nullptr);
  AbcUiClaimDesc native{};
  native.structSize = sizeof(native);
  native.role = ABCUI_CLAIM_GATE;
  native.canHandle = &onNativeVoiceDraw;
  if (!Api->claims->claimDomain(ABCUI_CHANNEL_HUD_NATIVE_DRAW, &native))
    Api->core->warn("voice: cannot claim the native voice status");
  Panel = moduleui::makePanel(kPanelName, kDocumentPath, 0);
  Panel.userdata = nullptr;
  Panel.onDocumentReady = &panelDocumentReady;
  Panel.onData = &panelOnData;
  Panel.onRedraw = nullptr;
  Panel.reset = &panelReset;
  Api->panels->registerPanel(&Panel);
}

void onModuleShutdown() {
  DocumentReady = false;
  ServerMuted = false;
  StatusBits = -1;
  clearRows();
}

void onModuleUnload() {
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (!AbcUiModule_Validate(
          api, ABCUI_MODULE_SECTION_CORE | ABCUI_MODULE_SECTION_CONSOLE |
                   ABCUI_MODULE_SECTION_EVENTS | ABCUI_MODULE_SECTION_PANELS |
                   ABCUI_MODULE_SECTION_UI | ABCUI_MODULE_SECTION_PLAYERS |
                   ABCUI_MODULE_SECTION_CLAIMS))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.voice";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
