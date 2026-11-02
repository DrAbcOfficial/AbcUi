// abc_ui_weapon_filter_module - the abcui.weaponfilter module driven through
// recording ABI seams: cvar watch registration, the five console commands,
// the config section load/save cycle, the read-only damaged-file session and
// the unload cleanup. No engine, no game install and no real config.json.
#include <abcui/module_api.h>

#include "framework/TestHarness.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" int ABCUI_CALL AbcUiModule_Main(uint32_t, const AbcUiModuleApi *,
                                           AbcUiModuleDescriptor *);

namespace {

struct Command {
  AbcUiCommandFn fn = nullptr;
  void *userdata = nullptr;
};

struct State {
  std::vector<std::string> prints;
  std::vector<std::string> warnings;
  std::vector<std::string> args;
  std::map<std::string, Command> commands;
  std::map<std::string, std::string> sections;
  std::map<std::string, std::pair<int, int>> overrides;
  AbcUiCommandFn watch = nullptr;
  void *watchData = nullptr;
  float cvar = 0.0f;
  bool enabled = false;
  int enabledCalls = 0;
  int clearAllCalls = 0;
  std::string clearedName;
  int writes = 0;
  bool unreadable = false;
  bool failInit = false;
};

State &state() {
  static State value;
  return value;
}

void collect(std::vector<std::string> &lines, const char *format,
             va_list args) {
  char buffer[1024];
  std::vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
  lines.emplace_back(buffer);
}

void apiPrint(const char *format, ...) {
  va_list args;
  va_start(args, format);
  collect(state().prints, format, args);
  va_end(args);
}

void apiWarn(const char *format, ...) {
  va_list args;
  va_start(args, format);
  collect(state().warnings, format, args);
  va_end(args);
}

void apiError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  collect(state().warnings, format, args);
  va_end(args);
}

int32_t apiReadSection(const char *name, char *out, uint32_t outSize,
                       uint32_t *required) {
  auto &s = state();
  if (required)
    *required = 0;
  if (!name)
    return ABCUI_ERROR_INVALID;
  if (s.unreadable)
    return ABCUI_ERROR_UNAVAILABLE;
  const auto it = s.sections.find(name);
  if (it == s.sections.end())
    return ABCUI_ERROR_NOT_FOUND;
  const uint32_t capacity = static_cast<uint32_t>(it->second.size()) + 1;
  if (required)
    *required = capacity;
  if (!out || outSize < capacity)
    return ABCUI_ERROR_LIMIT;
  std::memcpy(out, it->second.c_str(), capacity);
  return ABCUI_OK;
}

int32_t apiWriteSection(const char *name, const char *json, uint32_t jsonSize) {
  auto &s = state();
  if (!name || !json || jsonSize == 0)
    return ABCUI_ERROR_INVALID;
  if (s.unreadable)
    return ABCUI_ERROR_UNAVAILABLE;
  s.sections[name] = std::string(json, jsonSize);
  ++s.writes;
  return ABCUI_OK;
}

AbcUiCvarHandle apiRegisterCvarWatch(const char *, const char *, int,
                                     AbcUiCommandFn fn, void *userdata) {
  auto &s = state();
  s.watch = fn;
  s.watchData = userdata;
  return reinterpret_cast<AbcUiCvarHandle>(&s.cvar);
}

float apiCvarValue(AbcUiCvarHandle, float) {
  return state().cvar;
}

void apiRegisterCommand(const char *name, AbcUiCommandFn fn, void *userdata) {
  state().commands[name] = Command{fn, userdata};
}

int32_t apiArgc() {
  return static_cast<int32_t>(state().args.size());
}

const char *apiArgv(int32_t index) {
  const auto &args = state().args;
  if (index < 0 || static_cast<size_t>(index) >= args.size())
    return nullptr;
  return args[static_cast<size_t>(index)].c_str();
}

int32_t apiSetLayoutOverride(const char *name, int32_t slot, int32_t position) {
  if (!name)
    return ABCUI_ERROR_INVALID;
  state().overrides[name] = std::make_pair(slot, position);
  return ABCUI_OK;
}

int32_t apiClearLayoutOverride(const char *name) {
  auto &s = state();
  s.clearedName = name ? name : "";
  return s.overrides.erase(s.clearedName) > 0 ? ABCUI_OK
                                              : ABCUI_ERROR_NOT_FOUND;
}

void apiClearLayoutOverrides() {
  auto &s = state();
  ++s.clearAllCalls;
  s.overrides.clear();
}

void apiSetLayoutOverridesEnabled(int32_t enabled) {
  auto &s = state();
  ++s.enabledCalls;
  s.enabled = enabled != 0;
}

void run(const char *name, std::initializer_list<const char *> args) {
  auto &s = state();
  s.args.clear();
  s.args.emplace_back(name);
  for (const char *arg : args)
    s.args.emplace_back(arg);
  const auto it = s.commands.find(name);
  CHECK(it != s.commands.end());
  if (it != s.commands.end() && it->second.fn)
    it->second.fn(it->second.userdata);
}

} // namespace

int main() {
  auto &s = state();

  AbcUiCoreApi core{};
  core.structSize = sizeof(core);
  core.print = &apiPrint;
  core.warn = &apiWarn;
  core.error = &apiError;
  core.failInitialization = [](const char *) -> int32_t {
    state().failInit = true;
    return 1;
  };
  AbcUiConfigApi config{};
  config.structSize = sizeof(config);
  config.readSection = &apiReadSection;
  config.writeSection = &apiWriteSection;
  AbcUiConsoleApi console{};
  console.structSize = sizeof(console);
  console.registerCvarWatch = &apiRegisterCvarWatch;
  console.cvarValue = &apiCvarValue;
  console.registerCommand = &apiRegisterCommand;
  console.argc = &apiArgc;
  console.argv = &apiArgv;
  AbcUiWeaponsApi weapons{};
  weapons.structSize = sizeof(weapons);
  weapons.setLayoutOverride = &apiSetLayoutOverride;
  weapons.clearLayoutOverride = &apiClearLayoutOverride;
  weapons.clearLayoutOverrides = &apiClearLayoutOverrides;
  weapons.setLayoutOverridesEnabled = &apiSetLayoutOverridesEnabled;

  AbcUiModuleApi api{};
  api.structSize = sizeof(api);
  api.core = &core;
  api.console = &console;
  api.config = &config;
  api.weapons = &weapons;

  AbcUiModuleDescriptor module{};
  module.structSize = sizeof(module);
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &api, &module));
  if (!module.onLoad || !module.onInit || !module.onUnload)
    return abctest::finish("weaponfilter_module");

  TEST_CASE("init registers the cvar and the commands");
  module.onLoad();
  CHECK(!s.failInit);
  module.onInit();
  CHECK(s.commands.size() == 5);
  CHECK(s.commands.count("abc_ui_weapon_slot") == 1);
  CHECK(s.commands.count("abc_ui_weapon_slot_remove") == 1);
  CHECK(s.commands.count("abc_ui_weapon_slot_list") == 1);
  CHECK(s.commands.count("abc_ui_weapon_slot_clear") == 1);
  CHECK(s.commands.count("abc_ui_weapon_slot_reload") == 1);
  CHECK(s.watch != nullptr);
  CHECK(!s.enabled);
  CHECK(s.overrides.empty());
  // A missing section is not created by reading it.
  CHECK(s.sections.empty());

  TEST_CASE("a rule is saved and applied");
  run("abc_ui_weapon_slot", {"weapon_crowbar", "0", "0"});
  CHECK(s.overrides.size() == 1);
  CHECK(s.overrides.at("weapon_crowbar") == std::make_pair(0, 0));
  CHECK(s.sections.count("weapon_filter") == 1);
  const std::string saved = s.sections["weapon_filter"];
  CHECK(saved.find("\"version\":1") != std::string::npos);
  CHECK(saved.find("\"weapon_crowbar\"") != std::string::npos);
  CHECK(saved.find("\"slot\":0") != std::string::npos);
  CHECK(saved.find("\"position\":0") != std::string::npos);

  TEST_CASE("rules serialize sorted by name");
  run("abc_ui_weapon_slot", {"weapon_ins2coach", "3", "2"});
  const std::string sorted = s.sections["weapon_filter"];
  const size_t crowbar = sorted.find("weapon_crowbar");
  const size_t coach = sorted.find("weapon_ins2coach");
  CHECK(crowbar != std::string::npos);
  CHECK(coach != std::string::npos);
  CHECK(crowbar < coach);

  TEST_CASE("invalid arguments change nothing");
  const int writes = s.writes;
  run("abc_ui_weapon_slot", {"weapon_x"});
  run("abc_ui_weapon_slot", {"weapon_x", "10", "0"});
  run("abc_ui_weapon_slot", {"weapon_x", "0", "256"});
  run("abc_ui_weapon_slot", {"weapon-x", "0", "0"});
  run("abc_ui_weapon_slot", {"weapon_x", "slot", "0"});
  CHECK(s.writes == writes);
  CHECK(s.overrides.count("weapon_x") == 0);

  TEST_CASE("list reports the rules and the filter state");
  const size_t before = s.prints.size();
  run("abc_ui_weapon_slot_list", {});
  CHECK(s.prints.size() == before + 3);
  CHECK(s.prints[before].find("2 rule(s)") != std::string::npos);
  CHECK(s.prints[before].find("abc_ui_weapon_filter is off") !=
        std::string::npos);

  TEST_CASE("the cvar watch only flips the enabled state");
  s.cvar = 1.0f;
  s.watch(s.watchData);
  CHECK(s.enabled);
  CHECK(s.overrides.size() == 2);
  s.cvar = 0.0f;
  s.watch(s.watchData);
  CHECK(!s.enabled);
  CHECK(s.overrides.size() == 2);

  TEST_CASE("remove deletes one rule");
  run("abc_ui_weapon_slot_remove", {"weapon_crowbar"});
  CHECK(s.clearedName == "weapon_crowbar");
  CHECK(s.overrides.count("weapon_crowbar") == 0);
  CHECK(s.overrides.count("weapon_ins2coach") == 1);
  CHECK(s.sections["weapon_filter"].find("weapon_crowbar") ==
        std::string::npos);

  TEST_CASE("reload applies a manual edit");
  s.sections["weapon_filter"] = "{\"version\":1,\"weapons\":[{\"name\":"
                                "\"weapon_manual\",\"slot\":5,"
                                "\"position\":5}]}";
  s.clearAllCalls = 0;
  run("abc_ui_weapon_slot_reload", {});
  CHECK(s.clearAllCalls == 1);
  CHECK(s.overrides.size() == 1);
  CHECK(s.overrides.at("weapon_manual") == std::make_pair(5, 5));

  TEST_CASE("a damaged section warns and starts empty");
  s.sections["weapon_filter"] = "{not json";
  s.warnings.clear();
  run("abc_ui_weapon_slot_reload", {});
  CHECK(s.overrides.empty());
  CHECK(!s.warnings.empty());
  CHECK(s.sections["weapon_filter"] == "{not json");

  TEST_CASE("an unreadable file keeps the session in memory only");
  s.unreadable = true;
  s.warnings.clear();
  const int writesBefore = s.writes;
  run("abc_ui_weapon_slot_reload", {});
  CHECK(!s.warnings.empty());
  run("abc_ui_weapon_slot", {"weapon_session", "1", "1"});
  CHECK(s.writes == writesBefore);
  CHECK(s.overrides.count("weapon_session") == 1);
  CHECK(!s.warnings.empty());
  s.unreadable = false;

  TEST_CASE("unload clears the framework rules");
  module.onUnload();
  CHECK(s.overrides.empty());
  CHECK(!s.enabled);

  return abctest::finish("abc_ui_weapon_filter_module");
}
