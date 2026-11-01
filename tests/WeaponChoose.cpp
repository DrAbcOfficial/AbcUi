#include <abcui/events.h>
#include <abcui/module_api.h>

#include "framework/TestHarness.h"
#include "modsys/EventBus.h"

#include <cstring>
#include <string>
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
struct Recording {
  AbcUiPanelDesc panel{};
  AbcUiPanelDesc textPanel{};
  AbcUiEventSink choose = nullptr;
  void *chooseData = nullptr;
  bool visible = false;
  bool textMenu = false;
  bool fastSwitch = false;
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

int command(const char *name) {
  auto &state = recording();
  const int consumed = framework::eventBus().publishVetoAggregateRaw(
      (std::string("command.") + name).c_str(), nullptr);
  if (!consumed) {
    if (std::strcmp(name, "+attack") == 0)
      ++state.nativeAttacks;
    else
      ++state.nativeSelections;
  }
  return consumed;
}
} // namespace

int main() {
  auto &state = recording();
  AbcUiCoreApi core{};
  AbcUiPanelsApi panels{};
  panels.registerPanel = [](const AbcUiPanelDesc *panel) {
    if (std::strcmp(panel->name, "textmenu") == 0)
      recording().textPanel = *panel;
    else
      recording().panel = *panel;
    return 1;
  };
  AbcUiEventsApi events{};
  events.subscribe = [](const char *name, int, AbcUiEventSink sink,
                        void *data) -> AbcUiSubscription {
    if (std::strcmp(name, ABCUI_CHANNEL_HUD_CHOOSE_WEAPON) == 0) {
      recording().choose = sink;
      recording().chooseData = data;
    }
    return 1;
  };
  events.subscribeVeto = [](const char *name, int priority, AbcUiEventVeto sink,
                            void *userdata) -> AbcUiSubscription {
    return framework::eventBus().subscribeVeto(name, priority, sink, userdata);
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
  AbcUiCommandsApi commands{};
  commands.subscribeCommand = [](const char *name, int priority,
                                 AbcUiEventVeto sink,
                                 void *data) -> AbcUiSubscription {
    return framework::eventBus().subscribeVetoAggregate(
        (std::string("command.") + name).c_str(), priority, sink, data);
  };
  commands.textMenuOpen = [] { return recording().textMenu ? 1 : 0; };
  commands.setTextMenuOpen = [](int open) { recording().textMenu = open != 0; };
  AbcUiDataApi data{};
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
  weapons.fastSwitch = [] { return recording().fastSwitch ? 1 : 0; };
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
  api.commands = &commands;
  api.data = &data;
  api.ui = &ui;
  api.weapons = &weapons;
  AbcUiModuleDescriptor module{};
  module.structSize = sizeof(module);
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &api, &module));
  if (!module.onLoad)
    return abctest::finish("weaponchoose");
  AbcUiModuleDescriptor textModule{};
  textModule.structSize = sizeof(textModule);
  CHECK(AbcUiTextMenu_Main(ABCUI_ABI_VERSION, &api, &textModule));
  if (!textModule.onLoad)
    return abctest::finish("weaponchoose");
#ifdef ABCUI_TEST_TEXTMENU_FIRST
  textModule.onLoad();
#endif
  module.onLoad();
#ifndef ABCUI_TEST_TEXTMENU_FIRST
  textModule.onLoad();
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

  TEST_CASE("fast switch keeps selector closed");
  state.fastSwitch = true;
  CHECK(command("slot1") == 1);
  CHECK(!state.visible);
  CHECK(command("+attack") == 0);
  state.fastSwitch = false;

  TEST_CASE("all number keys select text menu items without selecting weapons");
  for (int slot = 1; slot <= 10; ++slot) {
    for (int bits : {0, 128}) {
      const AbcUiShowMenuEvent menu{1023, -1, bits, "Choose an item"};
      CHECK(framework::eventBus().publishVeto(ABCUI_CHANNEL_NETMSG_SHOW_MENU,
                                              menu));
      CHECK(state.textMenu);
      const int selections = state.weaponSelections;
      const int nativeSelections = state.nativeSelections;
      const size_t sent = state.menuCommands.size();
      CHECK(command(("slot" + std::to_string(slot)).c_str()) == 1);
      CHECK(state.menuCommands.size() == sent + 1);
      CHECK(state.menuCommands.back() ==
            std::string(bits ? "as_menuselect " : "menuselect ") +
                std::to_string(slot) + "\n");
      CHECK(!state.textMenu);
      CHECK(state.weaponSelections == selections);
      CHECK(state.nativeSelections == nativeSelections);
      CHECK(!state.visible);
      state.textPanel.onData(nullptr, 0.25);
      CHECK(!state.textVisible);
    }
  }

  TEST_CASE("invalid menu choices remain menu input");
  const AbcUiShowMenuEvent menu{1, -1, 0, "Only the first item"};
  CHECK(
      framework::eventBus().publishVeto(ABCUI_CHANNEL_NETMSG_SHOW_MENU, menu));
  const int selections = state.weaponSelections;
  const size_t sent = state.menuCommands.size();
  for (int slot = 2; slot <= 10; ++slot)
    CHECK(command(("slot" + std::to_string(slot)).c_str()) == 1);
  CHECK(state.textMenu);
  CHECK(state.menuCommands.size() == sent);
  CHECK(state.weaponSelections == selections);
  CHECK(command("slot1") == 1);
  state.textPanel.onData(nullptr, 0.25);
  CHECK(command("slot1") == 1);
  CHECK(state.visible);
  CHECK(command("+attack") == 1);
  CHECK(!state.visible);
  textModule.onShutdown();
  textModule.onUnload();
  module.onShutdown();
  module.onUnload();
  return abctest::finish("weaponchoose");
}
