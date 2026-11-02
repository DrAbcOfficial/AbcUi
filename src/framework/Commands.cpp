// framework::commands implementation - see Commands.h.
#include "Commands.h"

#include <charconv>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <metahook.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <abcui/events.h>

#include "../modsys/Arbitration.h"
#include "../modsys/Facts.h"
#include "../modsys/HookRegistry.h"

namespace commands {

namespace {

// MetaHook lookup is case-sensitive, while console execution is not. Retain
// exact engine spelling for HookCmd, but share one ASCII-folded domain.
std::string commandKey(const char *name) {
  std::string key;
  if (!name)
    return key;
  for (const auto *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) {
    if (*p <= ' ' || *p == 127 || *p == ';' || *p == '"')
      return {};
    key += static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p + ('a' - 'A') : *p);
  }
  return key;
}

int slotIndex(const std::string &key) {
  if (!key.starts_with("slot"))
    return -1;
  const std::string_view digits(key.data() + 4, key.size() - 4);
  if (digits.empty() || digits.size() > 2)
    return -1;
  int value = 0;
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (error != std::errc{} || end != digits.data() + digits.size())
    return -1;
  return value >= 1 && value <= 10 ? value - 1 : -1;
}

void commandThunk();

struct Entry {
  std::string name;
  std::string channel;
  xcommand_t original = nullptr;
  int slot = -1;
  bool hooked = false;
  bool shadowReported = false;
  unsigned dispatches = 0;
  unsigned handled = 0;
  int last = -1; // 1 handled, 0 chained to the predecessor, -1 never ran
};

struct Registry {
  ~Registry() {
    if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindCmd || !g_pMetaHookAPI->HookCmd)
      return;
    for (const auto &[key, entry] : entries) {
      (void)key;
      const auto *command = g_pMetaHookAPI->FindCmd(entry.name.c_str());
      // Never overwrite a later plugin's hook.
      if (command && command->function == &commandThunk)
        g_pMetaHookAPI->HookCmd(entry.name.c_str(), entry.original);
    }
  }
  std::map<std::string, Entry, std::less<>> entries;
  std::set<std::string, std::less<>> pending;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

cmd_function_t *findEngineCommand(const char *name, const std::string &key) {
  if (auto *command = g_pMetaHookAPI->FindCmd(name))
    return command;
  if (auto *command = g_pMetaHookAPI->FindCmd(key.c_str()))
    return command;
  if (!gEngfuncs.GetFirstCmdFunctionHandle ||
      !gEngfuncs.GetNextCmdFunctionHandle || !gEngfuncs.GetCmdFunctionName)
    return nullptr;
  for (auto handle = gEngfuncs.GetFirstCmdFunctionHandle(); handle;
       handle = gEngfuncs.GetNextCmdFunctionHandle(handle)) {
    const char *candidate = gEngfuncs.GetCmdFunctionName(handle);
    if (candidate && commandKey(candidate) == key)
      return g_pMetaHookAPI->FindCmd(candidate);
  }
  return nullptr;
}

const char *commandDomain(const char *name) {
  const std::string key = commandKey(name);
  if (key.empty())
    return nullptr;
  auto &entries = registry().entries;
  auto found = entries.find(key);
  if (found == entries.end()) {
    Entry entry;
    entry.name = name;
    entry.channel = "command." + key;
    entry.slot = slotIndex(key);
    found = entries.emplace(key, std::move(entry)).first;
  }
  return found->second.channel.c_str();
}

bool ensureBridge(const char *name) {
  const std::string key = commandKey(name);
  if (key.empty())
    return false;
  auto &entries = registry().entries;
  auto found = entries.find(key);
  if (found == entries.end()) {
    commandDomain(name);
    found = entries.find(key);
  }
  Entry &entry = found->second;
  if (entry.hooked)
    return true;
  if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindCmd || !g_pMetaHookAPI->HookCmd)
    return false;
  const auto *command = findEngineCommand(name, key);
  if (!command || !command->function) {
    registry().pending.insert(key);
    return false;
  }
  if (command->function == &commandThunk) {
    entry.hooked = true;
    entry.original = nullptr;
    registry().pending.erase(key);
    return true;
  }
  const xcommand_t previous =
      g_pMetaHookAPI->HookCmd(entry.name.c_str(), &commandThunk);
  if (!previous) {
    registry().pending.insert(key);
    return false;
  }
  entry.original = previous;
  entry.hooked = true;
  registry().pending.erase(key);
  framework::hooks::add(framework::hooks::Kind::kCommand, entry.name.c_str(),
                        reinterpret_cast<std::uintptr_t>(&commandThunk),
                        reinterpret_cast<std::uintptr_t>(previous));
  return true;
}

void commandThunk() {
  if (!gEngfuncs.Cmd_Argv)
    return;
  const std::string key = commandKey(gEngfuncs.Cmd_Argv(0));
  auto &entries = registry().entries;
  const auto found = entries.find(key);
  if (found == entries.end())
    return;
  Entry &entry = found->second;
  // Snapshot the predecessor before invoking any module code: a handler may
  // hook another command.
  const xcommand_t original = entry.original;
  ++entry.dispatches;
  const AbcUiCommandEvent event{entry.name.c_str()};
  const framework::arbitration::Decision decision =
      framework::arbitration::dispatch(entry.channel.c_str(), &event);
  framework::hooks::note(framework::hooks::Kind::kCommand, entry.name.c_str(),
                         decision.handled);
  if (decision.handled) {
    ++entry.handled;
    entry.last = 1;
    if ((decision.flags & framework::arbitration::kDecisionCloseMenu) != 0)
      framework::facts::clearMenu();
    if ((decision.flags & framework::arbitration::kDecisionChain) == 0)
      return;
  } else {
    entry.last = 0;
  }
  if (original)
    original();
  // The native text-menu path completes through the engine slot command:
  // close the framework fact for a valid item so the next number key returns
  // to the weapon selector.
  if (!decision.handled && entry.slot >= 0) {
    const auto &menu = framework::facts::menu();
    if (menu.visible && (menu.validSlots & (1 << entry.slot)) != 0)
      framework::facts::clearMenu();
  }
}

void hooksCommand() {
  for (const auto &entry : framework::hooks::snapshot()) {
    const auto line = std::format(
        "[AbcUi] {} {}: head={} generation={} dispatches={} handled={}",
        entry.kind == framework::hooks::Kind::kCommand ? "command" : "message",
        entry.name, entry.head ? "ours" : "other", entry.generation,
        entry.dispatches, entry.handled);
    abc::ConsoleWriteline(line.c_str());
  }
  for (const auto &claim : framework::arbitration::claims()) {
    const auto line = std::format(
        "[AbcUi] claim {} {} role={} priority={} owner={} active={}",
        claim.claim, claim.domain, static_cast<int>(claim.role), claim.priority,
        claim.owner, claim.active ? 1 : 0);
    abc::ConsoleWriteline(line.c_str());
  }
}

} // namespace

void init() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;
  framework::arbitration::setCommandProvider(&commandDomain, &ensureBridge);
  abc::ADD_COMMAND("abc_ui_hooks", &hooksCommand);
}

void pump() {
  if (registry().pending.empty())
    return;
  const std::vector<std::string> pending(registry().pending.begin(),
                                         registry().pending.end());
  for (const std::string &key : pending) {
    const auto found = registry().entries.find(key);
    if (found != registry().entries.end())
      ensureBridge(found->second.name.c_str());
  }
}

void refreshAfterClientLoad() {
  if (!g_pMetaHookAPI || !g_pMetaHookAPI->FindCmd || !g_pMetaHookAPI->HookCmd)
    return;
  for (auto &[key, entry] : registry().entries) {
    (void)key;
    if (!entry.hooked)
      continue;
    const auto *command = g_pMetaHookAPI->FindCmd(entry.name.c_str());
    if (!command) {
      framework::hooks::setHead(framework::hooks::Kind::kCommand,
                                entry.name.c_str(), false);
      continue;
    }
    if (command->function == &commandThunk) {
      framework::hooks::setHead(framework::hooks::Kind::kCommand,
                                entry.name.c_str(), true);
      continue;
    }
    // The client restored the displaced function: re-hook it so the bridge
    // survives a client-DLL reload. A foreign thunk is never displaced.
    if (entry.original && command->function == entry.original) {
      const xcommand_t previous =
          g_pMetaHookAPI->HookCmd(entry.name.c_str(), &commandThunk);
      if (previous) {
        entry.original = previous;
        framework::hooks::add(framework::hooks::Kind::kCommand,
                              entry.name.c_str(),
                              reinterpret_cast<std::uintptr_t>(&commandThunk),
                              reinterpret_cast<std::uintptr_t>(previous));
      }
      continue;
    }
    framework::hooks::setHead(framework::hooks::Kind::kCommand,
                              entry.name.c_str(), false);
    if (!entry.shadowReported) {
      entry.shadowReported = true;
      abc::ConsoleWarn("[AbcUi] command \"%s\" is hooked by another plugin; "
                       "AbcUi stays in its predecessor chain\n",
                       entry.name.c_str());
    }
  }
}

} // namespace commands
