// abcui.weaponfilter - client-side weapon selector slot/position rules.
//
// The module owns the policy and the persistence; the framework owns the
// layout. abc_ui_weapon_filter enables the rules, five console commands edit
// them and config.json remembers them in the module section. Nothing is sent
// to the server: the confirmed weapon command stays the server WeaponList
// name, and the override only changes where the selector shows the weapon.
//
// Rules are applied through the weapons api, which recomputes the effective
// layout and publishes hud.weaponLayout; an open selector re-groups itself.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include "WeaponFilterModel.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace {

const AbcUiModuleApi *Api = nullptr;
AbcUiCvarHandle FilterCvar = nullptr;
weaponfilter::Model Rules;
// The config file exists but cannot be read: rules apply in memory only.
bool ConfigReadOnly = false;

constexpr const char *kModuleId = "abcui.weaponfilter";
constexpr const char *kModuleVersion = "3.0.0";
// Old naming contract of the selector settings (never rename).
constexpr const char *kCvarName = "abc_ui_weapon_filter";
constexpr const char *kSection = "weapon_filter";
constexpr uint32_t kMaxSectionBytes = 64 * 1024;

constexpr const char *kUsage =
    "usage: abc_ui_weapon_slot <weapon> <slot 0-9> <position 0-255>\n"
    "       abc_ui_weapon_slot_remove <weapon>\n"
    "       abc_ui_weapon_slot_list\n"
    "       abc_ui_weapon_slot_clear\n"
    "       abc_ui_weapon_slot_reload";

int argumentCount() {
  return (Api && Api->console && Api->console->argc) ? Api->console->argc() : 0;
}

std::string_view argument(int index) {
  if (!Api || !Api->console || !Api->console->argv)
    return {};
  const char *value = Api->console->argv(index);
  return value ? std::string_view(value) : std::string_view{};
}

bool parseInt(std::string_view text, int &value) {
  if (text.empty())
    return false;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

bool filterEnabled() {
  return FilterCvar && Api && Api->console &&
         Api->console->cvarValue(FilterCvar, 0.0f) > 0.0f;
}

// Writes the model into the module section. A failure keeps the in-memory
// rules applied; it only reports that the session will not be remembered.
bool saveRules() {
  if (!Api || !Api->config)
    return false;
  if (ConfigReadOnly) {
    Api->core->warn("the config file is read-only; rules apply to this session "
                    "only");
    return false;
  }
  std::string json;
  std::string error;
  if (!Rules.serialize(json, error)) {
    Api->core->warn("%s", error.c_str());
    return false;
  }
  const int32_t status = Api->config->writeSection(
      kSection, json.c_str(), static_cast<uint32_t>(json.size()));
  if (status != ABCUI_OK) {
    Api->core->warn("the rules could not be saved (%d)", status);
    return false;
  }
  return true;
}

// Reads the module section into the model. A missing section is an empty rule
// set; an unreadable or oversized one warns and leaves the session writable
// only in memory.
void loadRules() {
  Rules.clear();
  ConfigReadOnly = false;
  if (!Api || !Api->config)
    return;

  uint32_t required = 0;
  int32_t status = Api->config->readSection(kSection, nullptr, 0, &required);
  if (status == ABCUI_ERROR_NOT_FOUND)
    return;
  if (status == ABCUI_ERROR_UNAVAILABLE) {
    ConfigReadOnly = true;
    Api->core->warn("the config file is unreadable; rules start empty and are "
                    "not saved");
    return;
  }
  if (status != ABCUI_ERROR_LIMIT || required == 0) {
    Api->core->warn("the saved rules could not be read (%d)", status);
    return;
  }
  if (required > kMaxSectionBytes) {
    Api->core->warn("the saved rules are too large; ignoring them");
    return;
  }

  std::string json(required, '\0');
  const uint32_t capacity = required;
  status = Api->config->readSection(kSection, json.data(), capacity, &required);
  if (status != ABCUI_OK) {
    Api->core->warn("the saved rules could not be read (%d)", status);
    return;
  }
  json.resize(capacity - 1);

  std::string error;
  if (!Rules.parse(json, error)) {
    Api->core->warn("the saved rules are damaged (%s); starting empty",
                    error.c_str());
    Rules.clear();
  }
}

// Hands the model to the framework. Enabling first publishes the server
// layout once, then every rule republishes; the selector only cares about the
// final state.
void applyRules() {
  if (!Api || !Api->weapons)
    return;
  Api->weapons->setLayoutOverridesEnabled(filterEnabled() ? 1 : 0);
  for (const auto &rule : Rules.rules())
    Api->weapons->setLayoutOverride(rule.second.name.c_str(), rule.second.slot,
                                    rule.second.position);
}

void commandSet(void *) {
  if (argumentCount() != 4) {
    Api->core->print("%s", kUsage);
    return;
  }
  const std::string name(argument(1));
  int slot = 0;
  int position = 0;
  if (!parseInt(argument(2), slot) || !parseInt(argument(3), position)) {
    Api->core->print("%s", kUsage);
    return;
  }

  weaponfilter::Rule rule;
  rule.name = name;
  rule.slot = slot;
  rule.position = position;
  std::string error;
  if (!Rules.set(rule, error)) {
    Api->core->print("%s", error.c_str());
    return;
  }
  const bool saved = saveRules();
  const int32_t status = Api->weapons->setLayoutOverride(
      rule.name.c_str(), rule.slot, rule.position);
  if (status != ABCUI_OK)
    Api->core->warn("the framework refused the rule (%d)", status);
  Api->core->print("%s -> slot %d, position %d%s", rule.name.c_str(), rule.slot,
                   rule.position, saved ? "" : " (not saved)");
}

void commandRemove(void *) {
  if (argumentCount() != 2) {
    Api->core->print("%s", kUsage);
    return;
  }
  const std::string name(argument(1));
  if (!Rules.erase(name)) {
    Api->core->print("no rule for %s", name.c_str());
    return;
  }
  const bool saved = saveRules();
  Api->weapons->clearLayoutOverride(name.c_str());
  Api->core->print("removed the rule for %s%s", name.c_str(),
                   saved ? "" : " (not saved)");
}

void commandList(void *) {
  Api->core->print("%zu rule(s); %s is %s", Rules.rules().size(), kCvarName,
                   filterEnabled() ? "on" : "off");
  for (const auto &[name, rule] : Rules.rules())
    Api->core->print("  %s slot %d position %d", rule.name.c_str(), rule.slot,
                     rule.position);
}

void commandClear(void *) {
  Rules.clear();
  const bool saved = saveRules();
  Api->weapons->clearLayoutOverrides();
  Api->core->print("cleared every rule%s", saved ? "" : " (not saved)");
}

void commandReload(void *) {
  loadRules();
  Api->weapons->clearLayoutOverrides();
  applyRules();
  Api->core->print("reloaded %zu rule(s); %s is %s", Rules.rules().size(),
                   kCvarName, filterEnabled() ? "on" : "off");
}

// The watch only flips the framework's enabled state; the rules stay in its
// map so turning the filter back on restores the same layout.
void onFilterCvar(void *) {
  if (!Api || !Api->weapons)
    return;
  Api->weapons->setLayoutOverridesEnabled(filterEnabled() ? 1 : 0);
  Api->core->print("%s is %s", kCvarName, filterEnabled() ? "on" : "off");
}

void onModuleLoad() {
  if (!Api->config || Api->config->structSize < sizeof(AbcUiConfigApi) ||
      !Api->console || !Api->weapons ||
      !ABCUI_API_HAS_MEMBER(AbcUiConsoleApi, Api->console, argc) ||
      !ABCUI_API_HAS_MEMBER(AbcUiConsoleApi, Api->console, argv) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, Api->weapons, setLayoutOverride) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, Api->weapons,
                            clearLayoutOverride) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, Api->weapons,
                            clearLayoutOverrides) ||
      !ABCUI_API_HAS_MEMBER(AbcUiWeaponsApi, Api->weapons,
                            setLayoutOverridesEnabled)) {
    Api->core->failInitialization("framework lacks the weapon filter APIs");
  }
}

void onModuleInit() {
  FilterCvar = Api->console->registerCvarWatch(kCvarName, "0", ABCUI_CVAR_VALUE,
                                               &onFilterCvar, nullptr);
  if (!FilterCvar)
    Api->core->warn("could not register %s", kCvarName);
  Api->console->registerCommand("abc_ui_weapon_slot", &commandSet, nullptr);
  Api->console->registerCommand("abc_ui_weapon_slot_remove", &commandRemove,
                                nullptr);
  Api->console->registerCommand("abc_ui_weapon_slot_list", &commandList,
                                nullptr);
  Api->console->registerCommand("abc_ui_weapon_slot_clear", &commandClear,
                                nullptr);
  Api->console->registerCommand("abc_ui_weapon_slot_reload", &commandReload,
                                nullptr);
  loadRules();
  applyRules();
}

// Final teardown (module unload or failed initialization): the framework must
// not keep rules whose owner is gone. A HUD_Shutdown cycle deliberately keeps
// them, because the next map has to lay out the same way.
void onModuleUnload() {
  if (Api && Api->weapons) {
    Api->weapons->clearLayoutOverrides();
    Api->weapons->setLayoutOverridesEnabled(0);
  }
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int ABCUI_CALL
AbcUiModule_Main(uint32_t abiVersion, const AbcUiModuleApi *api,
                 AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api ||
      !AbcUiModule_InitDescriptor(out))
    return 0;
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->console ||
      !api->config || !api->weapons)
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = kModuleId;
  out->version = kModuleVersion;
  out->onLoad = &onModuleLoad;
  out->onInit = &onModuleInit;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = nullptr;
  out->onUnload = &onModuleUnload;
  return 1;
}
