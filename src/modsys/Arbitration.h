#pragma once

// framework::arbitration - entry-point ownership for commands, messages,
// keys and host queries.
//
// A domain owns one dispatch decision. Every record belongs to a domain and
// declares a role:
//   - Owner:    the first matching owner handles the event; the native path
//               does not run unless the handler asks for it.
//   - Fallback: runs only when no owner matched; handles the native path.
//   - Observer: runs after the decision, never affects it.
//   - Gate:     collective query (native HUD draw); any true hides the item.
//
// Predicates are pure: they run on a frozen decision pass before any handler
// executes, so one claim can never observe another claim's mid-dispatch state
// changes. Claims are ordered by (priority desc, owner id, owner sequence);
// registration order and module load order are not inputs.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <abcui/abi.h>

#include "ModuleResources.h"

namespace framework::arbitration {

enum class Role : int {
  kOwner = 0,
  kFallback = 1,
  kObserver = 2,
  kGate = 3,
};

enum class DomainMode : int {
  kExclusive = 0,
  kCollective = 1,
};

using Claim = std::uint64_t; // 0 is never issued.
using Predicate = int(ABCUI_CALL *)(void *userdata, const void *event);
using Handler = std::int32_t(ABCUI_CALL *)(void *userdata, const void *event);

// Handler result bits. A handler that returns kHandled owns the event; the
// host applies kCloseMenu to the framework menu fact. kChain asks the host to
// run the native path as well (Compose).
inline constexpr std::int32_t kDecisionNone = 0;
inline constexpr std::int32_t kDecisionHandled = 1 << 0;
inline constexpr std::int32_t kDecisionCloseMenu = 1 << 1;
inline constexpr std::int32_t kDecisionChain = 1 << 2;

struct Decision {
  bool handled = false;
  bool conflict = false;
  std::int32_t flags = kDecisionNone;
  Claim claim = 0;
  Role role = Role::kOwner;
};

struct ClaimInfo {
  Claim claim = 0;
  std::string domain;
  std::string owner;
  Role role = Role::kOwner;
  int priority = 0;
  bool active = true;
  std::uint64_t sequence = 0;
};

// Domains are process-lifetime. A module may only claim a registered domain
// (or a command domain created by the host's command provider).
bool registerDomain(const char *domain, DomainMode mode);
bool hasDomain(const char *domain);

Claim claim(const char *domain, Role role, int priority, Predicate canHandle,
            Handler handle, void *userdata);
void release(Claim claim);
void setActive(Claim claim, bool active);

// Exclusive dispatch: one owner or fallback handles; observers run after.
Decision dispatch(const char *domain, const void *event);
// Collective dispatch: any gate predicate true hides the queried element.
bool gate(const char *domain, const void *event);

// Command bridges are host-owned: the framework creates the engine hook and
// registers the command domain lazily (the MediaPlayer command is registered
// by the client after module load). The provider pair keeps this layer free
// of engine and MetaHook dependencies so the offline tests link it directly.
using CommandDomain = const char *(*)(const char *name);
using CommandBridge = bool (*)(const char *name);
void setCommandProvider(CommandDomain domain, CommandBridge bridge);
Claim claimCommand(const char *name, Role role, int priority,
                   Predicate canHandle, Handler handle, void *userdata);

// Diagnostics.
std::vector<ClaimInfo> claims();
std::string domainList();

} // namespace framework::arbitration
