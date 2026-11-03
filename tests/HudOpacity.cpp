// Production HUD modules driven through recording C ABI seams. No engine,
// game files, network, or animated-image decoder is involved.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include "framework/TestHarness.h"
#include "modsys/Arbitration.h"
#include "modsys/Facts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace framework::module_resources {
void reportException(const char *, const char *) {}
} // namespace framework::module_resources

extern "C" int ABCUI_CALL AbcUiModule_Main(uint32_t, const AbcUiModuleApi *,
                                           AbcUiModuleDescriptor *);

namespace {

struct Element {
  std::map<std::string, std::string> properties;
  std::vector<Element *> children;
  bool removed = false;
  // Scroll metrics as the layout engine would report them; the killfeed's
  // tail glide reads them and writes scrollTop back.
  float scrollTop = 0.0f;
  float scrollHeight = 0.0f;
  float clientHeight = 0.0f;
};

struct Document {
  std::map<std::string, Element> elements;
  Element root;
  bool visible = false;
};

struct Subscription {
  std::string channel;
  AbcUiEventSink sink = nullptr;
  void *userdata = nullptr;
};

struct RecordingUi {
  std::map<std::string, Document> documents;
  std::map<std::string, AbcUiPanelDesc> panels;
  std::map<std::string, float> cvars;
  std::vector<Subscription> subscriptions;
  std::vector<std::unique_ptr<Element>> rows;

  void publish(const char *channel, const void *payload) {
    for (const auto &entry : subscriptions) {
      if (entry.channel == channel && entry.sink)
        entry.sink(entry.userdata, payload);
    }
  }

  bool dispatch(const char *domain, const void *payload) {
    return framework::arbitration::dispatch(domain, payload).handled;
  }

  void frame(const char *name, double seconds) {
    const auto &panel = panels.at(name);
    if (panel.onData)
      panel.onData(panel.userdata, seconds);
  }
};

RecordingUi &recording() {
  static RecordingUi instance;
  return instance;
}

const char *fakeCommandDomain(const char *name) {
  static std::map<std::string, std::string> domains;
  auto found = domains.find(name);
  if (found == domains.end())
    found = domains.emplace(name, std::string("command.") + name).first;
  return found->second.c_str();
}

bool fakeCommandBridge(const char *) {
  return true;
}

Element *element(AbcUiElement *handle) {
  return reinterpret_cast<Element *>(handle);
}

Document *document(AbcUiDocument *handle) {
  return reinterpret_cast<Document *>(handle);
}

float opacity(const Element &value) {
  const auto found = value.properties.find("opacity");
  return found == value.properties.end()
             ? 1.0f
             : std::strtof(found->second.c_str(), nullptr);
}

void print(const char *, ...) {}

int copyText(const char *text, char *out, uint32_t size) {
  const int length = static_cast<int>(std::strlen(text));
  if (out && size)
    std::snprintf(out, size, "%s", text);
  return length;
}

AbcUiCvarHandle registerCvar(const char *name, const char *value, int) {
  return &recording()
              .cvars.try_emplace(name, std::strtof(value, nullptr))
              .first->second;
}

} // namespace

int main() {
  auto &state = recording();
  framework::arbitration::setCommandProvider(&fakeCommandDomain,
                                             &fakeCommandBridge);
  AbcUiCoreApi core{};
  core.structSize = sizeof(core);
  core.print = &print;
  core.warn = &print;
  core.error = &print;
  AbcUiPanelsApi panels{};
  panels.structSize = sizeof(panels);
  panels.registerPanel = [](const AbcUiPanelDesc *panel) {
    recording().panels.emplace(panel->name, *panel);
    recording().documents.try_emplace(panel->name);
    return 1;
  };
  AbcUiEventsApi events{};
  events.structSize = sizeof(events);
  events.subscribe = [](const char *channel, int, AbcUiEventSink sink,
                        void *userdata) -> AbcUiSubscription {
    recording().subscriptions.push_back({channel, sink, userdata});
    return recording().subscriptions.size();
  };
  AbcUiConsoleApi console{};
  console.structSize = sizeof(console);
  console.registerCvar = &registerCvar;
  console.registerCvarWatch = [](const char *name, const char *value, int flags,
                                 AbcUiCommandFn, void *) {
    return registerCvar(name, value, flags);
  };
  console.cvarValue = [](AbcUiCvarHandle handle, float fallback) {
    return handle ? *static_cast<float *>(handle) : fallback;
  };
  console.findCvar = [](const char *) -> AbcUiCvarHandle { return nullptr; };
  AbcUiClaimsApi claims{};
  claims.structSize = sizeof(claims);
  claims.claimCommand = [](const char *name, const AbcUiClaimDesc *desc) {
    return framework::arbitration::claimCommand(
        name, static_cast<framework::arbitration::Role>(desc->role),
        desc->priority, desc->canHandle, desc->handle, desc->userdata);
  };
  claims.claimDomain = [](const char *domain, const AbcUiClaimDesc *desc) {
    return framework::arbitration::claim(
        domain, static_cast<framework::arbitration::Role>(desc->role),
        desc->priority, desc->canHandle, desc->handle, desc->userdata);
  };
  claims.release = [](AbcUiClaim claim) {
    framework::arbitration::release(claim);
  };
  claims.setActive = [](AbcUiClaim claim, int32_t active) {
    framework::arbitration::setActive(claim, active != 0);
  };
  AbcUiFactsApi facts{};
  facts.structSize = sizeof(facts);
  facts.get = [](const char *name, void *out, uint32_t size) {
    return framework::facts::get(name, out, size) ? 1 : 0;
  };
  facts.generation = [](const char *name) {
    return framework::facts::generation(name);
  };
  AbcUiDataApi data{};
  data.structSize = sizeof(data);
  data.hudState = [](AbcUiHudState *out) {
    out->suit = 1;
    out->flashBattery = 100;
  };
  data.playerState = [](AbcUiPlayerState *out) { out->index = 1; };
  data.levelName = [](char *out, uint32_t size) {
    return copyText("", out, size);
  };
  AbcUiUiApi ui{};
  ui.structSize = sizeof(ui);
  ui.panelDocument = [](const char *name) {
    return reinterpret_cast<AbcUiDocument *>(&recording().documents.at(name));
  };
  ui.documentShow = [](AbcUiDocument *doc) { document(doc)->visible = true; };
  ui.documentHide = [](AbcUiDocument *doc) { document(doc)->visible = false; };
  ui.documentVisible = [](AbcUiDocument *doc) {
    return document(doc)->visible ? 1 : 0;
  };
  ui.documentSetProperty = [](AbcUiDocument *doc, const char *name,
                              const char *value) {
    document(doc)->root.properties[name] = value;
  };
  ui.querySelector = [](AbcUiDocument *doc, const char *id) {
    return reinterpret_cast<AbcUiElement *>(&document(doc)->elements[id]);
  };
  ui.elementSetProperty = [](AbcUiElement *target, const char *name,
                             const char *value) {
    element(target)->properties[name] = value;
  };
  ui.elementSetInnerRML = [](AbcUiElement *, const char *) {};
  ui.elementAppendRml = [](AbcUiElement *parent, const char *, const char *) {
    auto row = std::make_unique<Element>();
    Element *result = row.get();
    recording().rows.push_back(std::move(row));
    element(parent)->children.push_back(result);
    return reinterpret_cast<AbcUiElement *>(result);
  };
  ui.elementRemove = [](AbcUiElement *target) {
    element(target)->removed = true;
  };
  ui.elementScrollTop = [](AbcUiElement *target) {
    return element(target)->scrollTop;
  };
  ui.elementSetScrollTop = [](AbcUiElement *target, float top) {
    element(target)->scrollTop = top;
  };
  ui.elementScrollHeight = [](AbcUiElement *target) {
    return element(target)->scrollHeight;
  };
  ui.elementClientHeight = [](AbcUiElement *target) {
    return element(target)->clientHeight;
  };
  ui.elementChild = [](AbcUiElement *parent, int32_t index) {
    auto &children = element(parent)->children;
    return index >= 0 && static_cast<size_t>(index) < children.size()
               ? reinterpret_cast<AbcUiElement *>(children[index])
               : nullptr;
  };
  ui.hudSprite = [](const char *, char *atlas, uint32_t size, int32_t *rect) {
    copyText("fixture", atlas, size);
    std::fill_n(rect, 4, 0);
    return 1;
  };
  ui.escapeRml = &copyText;
  ui.translate = [](const char *, char *, uint32_t) { return 0; };
  AbcUiVfsApi vfs{};
  vfs.structSize = sizeof(vfs);
  AbcUiWeaponsApi weapons{};
  weapons.structSize = sizeof(weapons);
  weapons.ammoIcon = [](int32_t, AbcUiWeaponSprite *) {
    return ABCUI_ERROR_NOT_FOUND;
  };
  AbcUiTexturesApi textures{};
  textures.structSize = sizeof(textures);
  textures.upload = [](const void *, uint32_t, int32_t, int32_t, int32_t,
                       AbcUiTexture *) { return ABCUI_ERROR_UNAVAILABLE; };
  textures.release = [](AbcUiTexture) {};
  AbcUiModuleApi api{};
  api.structSize = sizeof(api);
  api.core = &core;
  api.panels = &panels;
  api.events = &events;
  api.console = &console;
  api.claims = &claims;
  api.facts = &facts;
  api.data = &data;
  api.ui = &ui;
  api.vfs = &vfs;
  api.weapons = &weapons;
  api.textures = &textures;
  AbcUiModuleDescriptor module{};
  module.structSize = sizeof(module);
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &api, &module));
  if (!module.onLoad)
    return abctest::finish("hud-opacity");
  module.onLoad();
  if (module.onInit)
    module.onInit();
  for (const auto &[name, panel] : state.panels)
    if (panel.onDocumentReady)
      panel.onDocumentReady(panel.userdata);

  const auto subscriptionCount = state.subscriptions.size();
  for (int cycle = 0; cycle < 2; ++cycle) {
    constexpr std::string_view kind = ABCUI_TEST_MODULE;
    if (kind == "textmenu") {
      TEST_CASE("textmenu-fades-and-hides");
      const AbcUiShowMenuEvent show{1, 5, 0, "Menu"};
      framework::facts::openMenu(show.validSlots, show.displaySeconds,
                                 show.bits, 0.0);
      CHECK(state.dispatch(ABCUI_CHANNEL_NETMSG_SHOW_MENU, &show));
      state.frame("textmenu", 0.05);
      CHECK_NEAR(opacity(state.documents.at("textmenu").root), 0.25, 0.001);
      state.frame("textmenu", 0.05);
      CHECK_NEAR(opacity(state.documents.at("textmenu").root), 0.5, 0.001);
      const AbcUiShowMenuEvent hide{0, 0, 0, ""};
      framework::facts::clearMenu();
      CHECK(state.dispatch(ABCUI_CHANNEL_NETMSG_SHOW_MENU, &hide));
      state.frame("textmenu", 0.05);
      CHECK_NEAR(opacity(state.documents.at("textmenu").root), 0.375, 0.001);
      state.frame("textmenu", 0.2);
      CHECK(!state.documents.at("textmenu").visible);
    } else if (kind == "killfeed") {
      TEST_CASE("killfeed-fades-before-removal");
      const AbcUiKillFeedEvent event{"victim", "attacker", "weapon"};
      CHECK(state.dispatch(ABCUI_CHANNEL_TEXTFEED_KILL_FEED, &event));
      CHECK(state.rows.size() == 1);
      // The arrival glide: the layout pass behind the append grows the
      // viewport's scrollable overflow (written straight onto the viewport
      // element here) and the panel eases toward the tail instead of letting
      // the reflow jump. One row over the 60dp viewport = 15dp of tail.
      Element &feed = state.documents.at("deadmsg").elements["feed"];
      feed.scrollHeight = 75.0f;
      feed.clientHeight = 60.0f;
      state.frame("deadmsg", 0.1);
      CHECK_NEAR(feed.scrollTop, 15.0f * (1.0f - std::exp(-0.1 / 0.075)), 0.01);
      state.frame("deadmsg", 1.0);
      CHECK_NEAR(feed.scrollTop, 15.0f, 0.01);
      state.frame("deadmsg", 6.0);
      state.frame("deadmsg", 0.25);
      CHECK_NEAR(opacity(*state.rows.back()), 0.5, 0.001);
      CHECK(!state.rows.back()->removed);
      state.frame("deadmsg", 0.25);
      CHECK(state.rows.back()->removed);
    } else if (kind == "motd") {
      TEST_CASE("motd-fades-before-hiding");
      const AbcUiMotdEvent content{1, "Message"};
      state.publish(ABCUI_CHANNEL_NETMSG_MOTD, &content);
      const AbcUiVguiMenuEvent show{5};
      CHECK(state.dispatch(ABCUI_CHANNEL_MENU_VGUI_MENU, &show));
      for (int frame = 0; frame < 10; ++frame)
        state.frame("motd", 0.5);
      state.frame("motd", 0.25);
      CHECK_NEAR(opacity(state.documents.at("motd").elements.at("box")), 0.75,
                 0.001);
      CHECK(state.documents.at("motd").visible);
      state.frame("motd", 1.0);
      CHECK(!state.documents.at("motd").visible);
    } else if (kind == "hudeffects") {
      TEST_CASE("danger-retargets-without-jumping");
      const AbcUiIntEvent health{10};
      state.publish(ABCUI_CHANNEL_NETMSG_HEALTH, &health);
      auto &danger = state.documents.at("effect").elements.at("danger");
      CHECK_NEAR(opacity(danger), 0.0, 0.001);
      state.frame("effect", 0.1);
      CHECK_NEAR(opacity(danger), (1.0 - 10.0 / 45.0) * 0.5, 0.001);
      state.publish(ABCUI_CHANNEL_NETMSG_HEALTH, &health);
      state.frame("effect", 0.1);
      CHECK_NEAR(opacity(danger), 1.0 - 10.0 / 45.0, 0.001);
    } else if (kind == "ammo") {
      TEST_CASE("pickup-fades-in-and-out");
      const AbcUiItemPickupEvent pickup{"item"};
      state.publish(ABCUI_CHANNEL_NETMSG_ITEM_PICKUP, &pickup);
      CHECK(state.rows.size() == 1);
      state.frame("itemstack", 0.05);
      CHECK_NEAR(opacity(*state.rows.back()), 0.5, 0.001);
      state.frame("itemstack", 0.05);
      CHECK_NEAR(opacity(*state.rows.back()), 1.0, 0.001);
      state.frame("itemstack", 4.9);
      state.frame("itemstack", 0.15);
      CHECK_NEAR(opacity(*state.rows.back()), 0.5, 0.001);
      state.frame("itemstack", 0.2);
      CHECK(state.rows.back()->removed);
    } else if (kind == "flashlight") {
      TEST_CASE("new-icon-fades-in");
      state.frame("flashlight", 0.05);
      CHECK_NEAR(
          opacity(state.documents.at("flashlight").elements.at("off-image")),
          0.265, 0.001);
      const AbcUiFlashlightEvent on{1, 90};
      state.publish(ABCUI_CHANNEL_NETMSG_FLASHLIGHT, &on);
      state.frame("flashlight", 0.05);
      CHECK_NEAR(
          opacity(state.documents.at("flashlight").elements.at("on-image")),
          0.375, 0.001);
      const auto &panel = state.panels.at("flashlight");
      panel.reset(panel.userdata);
      state.frame("flashlight", 0.05);
      CHECK_NEAR(
          opacity(state.documents.at("flashlight").elements.at("on-image")),
          0.75, 0.001);
    }
    if (module.onShutdown)
      module.onShutdown();
    CHECK(state.subscriptions.size() == subscriptionCount);
    if (cycle == 0) {
      state.rows.clear();
      for (auto &[name, doc] : state.documents)
        doc = {};
      for (const auto &[name, panel] : state.panels)
        if (panel.onDocumentReady)
          panel.onDocumentReady(panel.userdata);
    }
  }
  if (module.onUnload)
    module.onUnload();
  return abctest::finish(ABCUI_TEST_MODULE "-opacity");
}
