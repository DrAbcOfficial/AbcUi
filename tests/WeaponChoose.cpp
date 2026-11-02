#include <abcui/events.h>
#include <abcui/module_api.h>

#include "framework/TestHarness.h"
#include "modsys/Arbitration.h"
#include "modsys/Facts.h"

#include <charconv>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

extern "C" int ABCUI_CALL AbcUiModule_Main(uint32_t, const AbcUiModuleApi *,
                                           AbcUiModuleDescriptor *);
extern "C" int ABCUI_CALL AbcUiTextMenu_Main(uint32_t, const AbcUiModuleApi *,
                                             AbcUiModuleDescriptor *);

namespace framework::module_resources {
void reportException(const char *, const char *) {
  CHECK(false);
}
} // namespace framework::module_resources

namespace {

using framework::arbitration::Role;

struct Recording {
  AbcUiPanelDesc panel{};
  AbcUiPanelDesc textPanel{};
  AbcUiEventSink choose = nullptr;
  void *chooseData = nullptr;
  AbcUiEventSink layout = nullptr;
  void *layoutData = nullptr;
  bool visible = false;
  bool document = true;
  bool textVisible = false;
  int textDocument = 0;
  int selectedSlot = -1;
  int serverCommands = 0;
  int nativeSelections = 0;
  int nativeAttacks = 0;
  int weaponSelections = 0;
  std::string rml;
  std::vector<std::string> menuCommands;
};

Recording &recording() {
  static Recording value;
  return value;
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

void noPrint(const char *, ...) {}

int slotIndexOf(std::string_view name) {
  if (!name.starts_with("slot"))
    return -1;
  int value = 0;
  const std::string_view digits = name.substr(4);
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (error != std::errc{} || end != digits.data() + digits.size())
    return -1;
  return value >= 1 && value <= 10 ? value - 1 : -1;
}

int command(const char *name) {
  auto &state = recording();
  const AbcUiCommandEvent event{name};
  const std::string domain = std::string("command.") + name;
  const auto decision =
      framework::arbitration::dispatch(domain.c_str(), &event);
  if (decision.handled) {
    if ((decision.flags & framework::arbitration::kDecisionCloseMenu) != 0)
      framework::facts::clearMenu();
    return 1;
  }
  if (std::strcmp(name, "+attack") == 0) {
    ++state.nativeAttacks;
    return 0;
  }
  ++state.nativeSelections;
  // The host closes the framework menu fact when the native slot path handled
  // a valid item; mirror that for the following checks.
  const int slot = slotIndexOf(name);
  const auto &menu = framework::facts::menu();
  if (slot >= 0 && menu.visible && (menu.validSlots & (1 << slot)) != 0)
    framework::facts::clearMenu();
  return 0;
}

bool showMenu(int validSlots, int bits = 0) {
  if (validSlots == 0)
    framework::facts::clearMenu();
  else if ((bits & 0x7F) == 0)
    framework::facts::openMenu(validSlots, 15, bits, 0.0);
  const AbcUiShowMenuEvent event{validSlots, 15, bits, "Choose an item"};
  return framework::arbitration::dispatch(ABCUI_CHANNEL_NETMSG_SHOW_MENU,
                                          &event)
      .handled;
}

AbcUiClaim claimCommand(const char *name, const AbcUiClaimDesc *desc) {
  return framework::arbitration::claimCommand(
      name, static_cast<Role>(desc->role), desc->priority, desc->canHandle,
      desc->handle, desc->userdata);
}

AbcUiClaim claimDomain(const char *domain, const AbcUiClaimDesc *desc) {
  return framework::arbitration::claim(domain, static_cast<Role>(desc->role),
                                       desc->priority, desc->canHandle,
                                       desc->handle, desc->userdata);
}

} // namespace

int main() {
  auto &state = recording();
  framework::arbitration::setCommandProvider(&fakeCommandDomain,
                                             &fakeCommandBridge);

  AbcUiCoreApi core{};
  core.structSize = sizeof(core);
  core.print = &noPrint;
  core.warn = &noPrint;
  core.error = &noPrint;
  AbcUiPanelsApi panels{};
  panels.structSize = sizeof(panels);
  panels.registerPanel = [](const AbcUiPanelDesc *panel) {
    if (std::strcmp(panel->name, "textmenu") == 0)
      recording().textPanel = *panel;
    else
      recording().panel = *panel;
    return 1;
  };
  AbcUiEventsApi events{};
  events.structSize = sizeof(events);
  events.subscribe = [](const char *name, int, AbcUiEventSink sink,
                        void *data) -> AbcUiSubscription {
    if (std::strcmp(name, ABCUI_CHANNEL_HUD_CHOOSE_WEAPON) == 0) {
      recording().choose = sink;
      recording().chooseData = data;
    }
    if (std::strcmp(name, ABCUI_CHANNEL_HUD_WEAPON_LAYOUT) == 0) {
      recording().layout = sink;
      recording().layoutData = data;
    }
    return 1;
  };
  events.unsubscribe = [](AbcUiSubscription) {};
  events.publish = [](const char *, uint32_t, const void *, uint32_t) {
    return 1;
  };
  AbcUiConsoleApi console{};
  console.structSize = sizeof(console);
  console.registerCvar = [](const char *, const char *,
                            int) -> AbcUiCvarHandle { return nullptr; };
  console.playSound = [](const char *, float) {};
  console.serverCommand = [](const char *name) {
    CHECK(std::strcmp(name, "weapon_test") == 0);
    ++recording().serverCommands;
  };
  console.engineCommand = [](const char *text) {
    recording().menuCommands.emplace_back(text);
  };
  AbcUiClaimsApi claims{};
  claims.structSize = sizeof(claims);
  claims.claimCommand = &claimCommand;
  claims.claimDomain = &claimDomain;
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
    out->health = 100;
  };
  AbcUiUiApi ui{};
  ui.structSize = sizeof(ui);
  ui.panelDocument = [](const char *name) -> AbcUiDocument * {
    if (std::strcmp(name, "textmenu") == 0)
      return reinterpret_cast<AbcUiDocument *>(&recording().textDocument);
    return recording().document
               ? reinterpret_cast<AbcUiDocument *>(&recording())
               : nullptr;
  };
  ui.documentShow = [](AbcUiDocument *doc) {
    if (doc == reinterpret_cast<AbcUiDocument *>(&recording().textDocument))
      recording().textVisible = true;
    else
      recording().visible = true;
  };
  ui.documentHide = [](AbcUiDocument *doc) {
    if (doc == reinterpret_cast<AbcUiDocument *>(&recording().textDocument))
      recording().textVisible = false;
    else
      recording().visible = false;
  };
  ui.documentSetProperty = [](AbcUiDocument *, const char *, const char *) {};
  ui.querySelector = [](AbcUiDocument *, const char *) {
    return reinterpret_cast<AbcUiElement *>(&recording());
  };
  ui.elementSetProperty = [](AbcUiElement *, const char *, const char *) {};
  ui.elementSetInnerRML = [](AbcUiElement *, const char *rml) {
    recording().rml = rml;
  };
  AbcUiWeaponsApi weapons{};
  weapons.structSize = sizeof(weapons);
  weapons.fastSwitch = [] { return 0; };
  weapons.selectedSlot = [] { return recording().selectedSlot; };
  weapons.selected = [](AbcUiWeaponInfo *out) {
    if (recording().selectedSlot != 0)
      return 0;
    out->slot = 0;
    out->slotPos = 0;
    out->owned = 1;
    out->hasAmmo = 1;
    std::strcpy(out->name, "weapon_test");
    return 1;
  };
  weapons.validCommandName = [](const char *) { return 1; };
  weapons.ammoIcon = [](int32_t, AbcUiWeaponSprite *) -> int32_t {
    return ABCUI_ERROR_NOT_FOUND;
  };
  weapons.list = [](void *, uint32_t, uint32_t) -> int32_t { return 0; };
  weapons.selectSlot = [](int32_t slot, int32_t, int32_t wheel) {
    auto &value = recording();
    ++value.weaponSelections;
    value.selectedSlot = wheel ? 0 : slot;
    const AbcUiWeaponEvent event{value.selectedSlot,
                                 value.selectedSlot == 0 ? 0 : -1};
    value.choose(value.chooseData, &event);
  };

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
  api.weapons = &weapons;
  AbcUiModuleDescriptor module{};
  module.structSize = sizeof(module);
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &api, &module));
  if (!module.onLoad)
    return abctest::finish("weaponchoose");
#ifndef ABCUI_TEST_TEXTMENU_ABSENT
  AbcUiModuleDescriptor textModule{};
  textModule.structSize = sizeof(textModule);
  CHECK(AbcUiTextMenu_Main(ABCUI_ABI_VERSION, &api, &textModule));
  if (!textModule.onLoad)
    return abctest::finish("weaponchoose");
#ifdef ABCUI_TEST_TEXTMENU_FIRST
  textModule.onLoad();
#endif
#endif
  module.onLoad();
#ifndef ABCUI_TEST_TEXTMENU_ABSENT
#ifndef ABCUI_TEST_TEXTMENU_FIRST
  textModule.onLoad();
#endif
#endif
  module.onInit();

  TEST_CASE("confirm does not leave a hidden native selection");
  CHECK(command("slot1") == 1);
  CHECK(state.visible);
  CHECK(command("+attack") == 1);
  CHECK(!state.visible);
  CHECK(state.serverCommands == 1);
  CHECK(command("+attack") == 0);
  CHECK(command("+attack") == 0);
  CHECK(state.serverCommands == 1);
  CHECK(state.nativeAttacks == 2);
  CHECK(state.nativeSelections == 0);

  TEST_CASE("empty slot opens without confirming the previous weapon");
  CHECK(command("slot2") == 1);
  CHECK(state.visible);
  CHECK(state.rml.find("slot2_icon") != std::string::npos);
  CHECK(state.rml.find("id='bucket'") == std::string::npos);
  CHECK(command("+attack") == 0);
  CHECK(state.serverCommands == 1);
  state.panel.onData(nullptr, 6.0);
  CHECK(!state.visible);
  CHECK(command("slot2") == 1);
  CHECK(state.visible);

  TEST_CASE("layout change refreshes an open bar only");
  CHECK(state.layout != nullptr);
  state.rml = "sentinel";
  state.layout(state.layoutData, nullptr);
  CHECK(state.rml != "sentinel");
  CHECK(state.rml.find("slot2_icon") != std::string::npos);
  CHECK(state.visible);
  state.panel.onData(nullptr, 6.0);
  CHECK(!state.visible);
  state.rml = "hidden";
  state.layout(state.layoutData, nullptr);
  CHECK(state.rml == "hidden");

  TEST_CASE("wheel opens and confirms without native selection");
  for (const char *name : {"invnext", "invprev"}) {
    CHECK(command(name) == 1);
    CHECK(state.visible);
    CHECK(command("+attack") == 1);
    CHECK(!state.visible);
    CHECK(command("+attack") == 0);
  }
  CHECK(state.nativeSelections == 0);
  CHECK(state.serverCommands == 3);

  TEST_CASE("missing document retains native slot handling");
  state.document = false;
  CHECK(command("slot1") == 0);
  CHECK(command("invnext") == 0);
  CHECK(command("invprev") == 0);
  state.document = true;

#ifdef ABCUI_TEST_TEXTMENU_ABSENT
  TEST_CASE("a menu without a renderer keeps the native slot path");
  CHECK(!showMenu(1023));
  CHECK(framework::facts::menu().visible);
  const int selections = state.weaponSelections;
  const int native = state.nativeSelections;
  CHECK(command("slot1") == 0);
  CHECK(state.weaponSelections == selections);
  CHECK(state.nativeSelections == native + 1);
  CHECK(!framework::facts::menu().visible);
#else
  TEST_CASE("all number keys select text menu items without selecting weapons");
  for (int slot = 1; slot <= 10; ++slot) {
    for (int bits : {0, 128}) {
      CHECK(showMenu(1023, bits));
      CHECK(framework::facts::menu().visible);
      const int selections = state.weaponSelections;
      const int native = state.nativeSelections;
      const size_t sent = state.menuCommands.size();
      CHECK(command(("slot" + std::to_string(slot)).c_str()) == 1);
      CHECK(state.menuCommands.size() == sent + 1);
      CHECK(state.menuCommands.back() ==
            std::string(bits ? "as_menuselect " : "menuselect ") +
                std::to_string(slot) + "\n");
      CHECK(!framework::facts::menu().visible);
      CHECK(state.weaponSelections == selections);
      CHECK(state.nativeSelections == native);
      CHECK(!state.visible);
      state.textPanel.onData(nullptr, 0.25);
      CHECK(!state.textVisible);
    }
  }

  TEST_CASE("invalid menu choices remain menu input");
  CHECK(showMenu(1));
  const int selections = state.weaponSelections;
  const size_t sent = state.menuCommands.size();
  for (int slot = 2; slot <= 10; ++slot)
    CHECK(command(("slot" + std::to_string(slot)).c_str()) == 1);
  CHECK(framework::facts::menu().visible);
  CHECK(state.menuCommands.size() == sent);
  CHECK(state.weaponSelections == selections);
  CHECK(command("slot1") == 1);
  state.textPanel.onData(nullptr, 0.25);
  CHECK(command("slot1") == 1);
  CHECK(state.visible);
  CHECK(command("+attack") == 1);
  CHECK(!state.visible);
#endif

#ifndef ABCUI_TEST_TEXTMENU_ABSENT
  textModule.onShutdown();
  textModule.onUnload();
#endif
  module.onShutdown();
  module.onUnload();
  return abctest::finish("weaponchoose");
}
