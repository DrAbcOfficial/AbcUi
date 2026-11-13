#pragma once

// Shared recording seams for the HUD panel modules (abcui.health and
// abcui.hudeffects): a fake AbcUiModuleApi carrying exactly the sections those
// modules declare, a tiny DOM that records label text, inline properties and
// class attributes, and a controllable HUD/player snapshot. No engine, no
// RmlUi context and no game installation.

#include <abcui/events.h>
#include <abcui/module_api.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace abctest::hudseams {

struct Element {
  std::string id;
  std::string innerRml;
  std::string className;
  std::map<std::string, std::string> properties;
};

struct Subscription {
  std::string channel;
  AbcUiEventSink sink = nullptr;
  void *userdata = nullptr;
};

struct State {
  // Snapshot handed back by data->hudState / data->playerState.
  AbcUiHudState hud{};
  int32_t playerIndex = 1;
  // Every cvar reads through this value (hud_danger_health / hud_efx).
  float cvarValue = 45.0f;
  // The descriptor the module registered; the tests drive its callbacks.
  AbcUiPanelDesc panel{};
  const AbcUiPanelDesc *registered = nullptr;
  AbcUiModuleDescriptor descriptor{};
  std::vector<Subscription> subscriptions;
  std::map<std::string, Element> elements;
  std::vector<std::string> warnings;
  std::string claimDomain;
  int writes = 0;      // every DOM write (property, inner RML, attribute)
  int textWrites = 0;  // elementSetInnerRML only (a changed number)
  int classWrites = 0; // elementSetAttribute only (a changed bar fill)
  int shows = 0;
  int hides = 0;
  bool visible = false;
  int claims = 0;
};

inline State &state() {
  static State value;
  return value;
}

// Clears every recording and restores the descriptor struct sizes.
inline void reset() {
  state() = State{};
  state().hud.structSize = sizeof(AbcUiHudState);
  state().descriptor.structSize = sizeof(AbcUiModuleDescriptor);
  state().panel.structSize = sizeof(AbcUiPanelDesc);
}

// The element a module has driven, by id (created on the first query).
inline Element *element(const char *id) {
  auto &elements = state().elements;
  const auto found = elements.find(id ? id : "");
  return found == elements.end() ? nullptr : &found->second;
}

inline std::string elementText(const char *id) {
  const Element *found = element(id);
  return found ? found->innerRml : std::string();
}

inline std::string elementProperty(const char *id, const char *name) {
  const Element *found = element(id);
  if (!found)
    return std::string();
  const auto foundProperty = found->properties.find(name ? name : "");
  return foundProperty == found->properties.end() ? std::string()
                                                  : foundProperty->second;
}

// Delivers one framework data event to every subscription on the channel.
inline void publish(const char *channel, const void *payload) {
  for (const Subscription &subscription : state().subscriptions)
    if (subscription.channel == channel && subscription.sink)
      subscription.sink(subscription.userdata, payload);
}

inline void publishHealth(int value) {
  const AbcUiIntEvent event{value};
  publish(ABCUI_CHANNEL_NETMSG_HEALTH, &event);
}

// ---- core ---------------------------------------------------------------

inline void corePrint(const char *, ...) {}

inline void coreWarn(const char *format, ...) {
  char buffer[512] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
  va_end(args);
  state().warnings.emplace_back(buffer);
}

// ---- console ------------------------------------------------------------

inline AbcUiCvarHandle registerCvar(const char *, const char *, int) {
  static int handles[8] = {};
  static int next = 0;
  return &handles[next++ % 8];
}

inline float cvarValue(AbcUiCvarHandle, float) { return state().cvarValue; }

inline AbcUiCvarHandle findCvar(const char *) {
  static int handle = 0;
  return &handle;
}

// ---- events -------------------------------------------------------------

inline AbcUiSubscription subscribe(const char *channel, int,
                                   AbcUiEventSink sink, void *userdata) {
  state().subscriptions.push_back({channel ? channel : "", sink, userdata});
  return static_cast<AbcUiSubscription>(state().subscriptions.size());
}

inline void unsubscribe(AbcUiSubscription) {}

// ---- panels -------------------------------------------------------------

inline int registerPanel(const AbcUiPanelDesc *desc) {
  if (!desc)
    return 0;
  state().panel = *desc;
  state().registered = &state().panel;
  return 1;
}

// ---- data ---------------------------------------------------------------

inline void hudState(AbcUiHudState *out) {
  if (!out)
    return;
  const AbcUiHudState &hud = state().hud;
  out->suit = hud.suit;
  out->spectate = hud.spectate;
  out->longjump = hud.longjump;
  out->hideBits = hud.hideBits;
  out->flashOn = hud.flashOn;
  out->flashBattery = hud.flashBattery;
  out->health = hud.health;
  out->armor = hud.armor;
  out->waterLevel = hud.waterLevel;
  out->fov = hud.fov;
  out->punchAngle[0] = hud.punchAngle[0];
  out->punchAngle[1] = hud.punchAngle[1];
  out->punchAngle[2] = hud.punchAngle[2];
  out->spectateMode = hud.spectateMode;
  out->spectateTarget = hud.spectateTarget;
}

inline void playerState(AbcUiPlayerState *out) {
  if (!out)
    return;
  out->index = state().playerIndex;
}

// ---- ui -----------------------------------------------------------------

inline AbcUiDocument *panelDocument(const char *name) {
  const AbcUiPanelDesc *panel = state().registered;
  if (!panel || !name || std::strcmp(name, panel->name) != 0)
    return nullptr;
  static int document = 0;
  return reinterpret_cast<AbcUiDocument *>(&document);
}

inline void documentShow(AbcUiDocument *) {
  state().visible = true;
  ++state().shows;
}

inline void documentHide(AbcUiDocument *) {
  state().visible = false;
  ++state().hides;
}

inline AbcUiElement *querySelector(AbcUiDocument *, const char *id) {
  Element &slot = state().elements[id ? id : ""];
  slot.id = id ? id : "";
  return reinterpret_cast<AbcUiElement *>(&slot);
}

inline Element *fromHandle(AbcUiElement *handle) {
  return reinterpret_cast<Element *>(handle);
}

inline void setProperty(AbcUiElement *handle, const char *name,
                        const char *value) {
  Element *slot = fromHandle(handle);
  if (!slot || !name)
    return;
  slot->properties[name] = value ? value : "";
  ++state().writes;
}

inline void setInnerRml(AbcUiElement *handle, const char *rml) {
  Element *slot = fromHandle(handle);
  if (!slot)
    return;
  slot->innerRml = rml ? rml : "";
  ++state().writes;
  ++state().textWrites;
}

inline void setAttribute(AbcUiElement *handle, const char *name,
                         const char *value) {
  Element *slot = fromHandle(handle);
  if (!slot || !name)
    return;
  if (std::strcmp(name, "class") == 0) {
    slot->className = value ? value : "";
    ++state().classWrites;
  }
  ++state().writes;
}

inline void restartAnimation(AbcUiElement *) {}

inline void formatColor(const float rgb[3], char *out, uint32_t outSize) {
  std::snprintf(out, outSize, "rgb(%.0f,%.0f,%.0f)",
                 static_cast<double>(rgb[0]), static_cast<double>(rgb[1]),
                 static_cast<double>(rgb[2]));
}

inline void differColor(const float base[3], const float danger[3], float ratio,
                        char *out, uint32_t outSize) {
  char text[64] = {};
  std::snprintf(text, sizeof(text), "rgb(%.0f,%.0f,%.0f)",
                static_cast<double>(danger[0] + (base[0] - danger[0]) * ratio),
                static_cast<double>(danger[1] + (base[1] - danger[1]) * ratio),
                static_cast<double>(danger[2] + (base[2] - danger[2]) * ratio));
  std::snprintf(out, outSize, "%s", text);
}

// ---- claims -------------------------------------------------------------

inline AbcUiClaim claimDomain(const char *domain, const AbcUiClaimDesc *) {
  ++state().claims;
  state().claimDomain = domain ? domain : "";
  return 1;
}

// ---- table --------------------------------------------------------------

// The module keeps the table pointer for its whole lifetime, so the fake must
// outlive every call: one function-local instance, filled on first use.
inline AbcUiModuleApi &api() {
  static AbcUiCoreApi core{};
  static AbcUiConsoleApi console{};
  static AbcUiEventsApi events{};
  static AbcUiPanelsApi panels{};
  static AbcUiDataApi data{};
  static AbcUiUiApi ui{};
  static AbcUiClaimsApi claims{};
  static AbcUiModuleApi table{};
  static bool built = false;
  if (built)
    return table;
  built = true;

  core.structSize = sizeof(core);
  core.frameworkVersion = ABCUI_VERSION_PACK(0, 11, 1);
  core.frameworkVersionString = "offline-test";
  core.print = &corePrint;
  core.warn = &coreWarn;
  core.error = &coreWarn;

  console.structSize = sizeof(console);
  console.registerCvar = &registerCvar;
  console.cvarValue = &cvarValue;
  console.findCvar = &findCvar;

  events.structSize = sizeof(events);
  events.subscribe = &subscribe;
  events.unsubscribe = &unsubscribe;

  panels.structSize = sizeof(panels);
  panels.registerPanel = &registerPanel;

  data.structSize = sizeof(data);
  data.hudState = &hudState;
  data.playerState = &playerState;

  ui.structSize = sizeof(ui);
  ui.panelDocument = &panelDocument;
  ui.documentShow = &documentShow;
  ui.documentHide = &documentHide;
  ui.querySelector = &querySelector;
  ui.elementSetProperty = &setProperty;
  ui.elementSetInnerRML = &setInnerRml;
  ui.elementSetAttribute = &setAttribute;
  ui.elementRestartAnimation = &restartAnimation;
  ui.formatColor = &formatColor;
  ui.differColor = &differColor;

  claims.structSize = sizeof(claims);
  claims.claimDomain = &claimDomain;

  table.structSize = sizeof(table);
  table.abiVersion = ABCUI_ABI_VERSION;
  table.core = &core;
  table.console = &console;
  table.events = &events;
  table.panels = &panels;
  table.data = &data;
  table.ui = &ui;
  table.claims = &claims;
  return table;
}

} // namespace abctest::hudseams
