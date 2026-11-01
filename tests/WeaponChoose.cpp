#include <abcui/events.h>
#include <abcui/module_api.h>

#include "framework/TestHarness.h"

#include <cstring>
#include <map>
#include <string>

extern "C" int ABCUI_CALL AbcUiModule_Main(uint32_t, const AbcUiModuleApi *,
                                           AbcUiModuleDescriptor *);

namespace {
struct Command {
  AbcUiEventVeto sink;
  void *userdata;
};

struct Recording {
  AbcUiPanelDesc panel{};
  std::map<std::string, Command> commands;
  AbcUiEventSink choose = nullptr;
  void *chooseData = nullptr;
  bool visible = false;
  bool textMenu = false;
  bool fastSwitch = false;
  bool document = true;
  int selectedSlot = -1;
  int serverCommands = 0;
  int nativeSelections = 0;
  int nativeAttacks = 0;
  std::string rml;
};

Recording &recording() {
  static Recording value;
  return value;
}

int command(const char *name) {
  auto &state = recording();
  const auto &entry = state.commands.at(name);
  const int consumed = entry.sink(entry.userdata, nullptr);
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
  events.subscribeVeto = [](const char *, int, AbcUiEventVeto,
                            void *) -> AbcUiSubscription { return 1; };
  AbcUiConsoleApi console{};
  console.structSize = sizeof(console);
  console.registerCvar = [](const char *, const char *,
                            int) -> AbcUiCvarHandle { return nullptr; };
  console.playSound = [](const char *, float) {};
  console.serverCommand = [](const char *name) {
    CHECK(std::strcmp(name, "weapon_test") == 0);
    ++recording().serverCommands;
  };
  AbcUiCommandsApi commands{};
  commands.subscribeCommand = [](const char *name, int, AbcUiEventVeto sink,
                                 void *data) -> AbcUiSubscription {
    recording().commands.emplace(name, Command{sink, data});
    return 1;
  };
  commands.textMenuOpen = [] { return recording().textMenu ? 1 : 0; };
  AbcUiDataApi data{};
  data.hudState = [](AbcUiHudState *out) {
    out->suit = 1;
    out->health = 100;
  };
  AbcUiUiApi ui{};
  ui.structSize = sizeof(ui);
  ui.panelDocument = [](const char *) -> AbcUiDocument * {
    return recording().document
               ? reinterpret_cast<AbcUiDocument *>(&recording())
               : nullptr;
  };
  ui.documentShow = [](AbcUiDocument *) { recording().visible = true; };
  ui.documentHide = [](AbcUiDocument *) { recording().visible = false; };
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
  module.onLoad();
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

  TEST_CASE("text menu and missing document retain native slot handling");
  state.textMenu = true;
  CHECK(command("slot1") == 0);
  CHECK(!state.visible);
  state.textMenu = false;
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
  module.onShutdown();
  module.onUnload();
  return abctest::finish("weaponchoose");
}
