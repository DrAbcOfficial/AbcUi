// framework::arbitration implementation - see Arbitration.h.
#include "Arbitration.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <abcui/events.h>

namespace framework::arbitration {
namespace {

struct Record {
  Claim claim = 0;
  std::string domain;
  std::string ownerId;
  Role role = Role::kOwner;
  int priority = 0;
  bool active = true;
  std::uint64_t sequence = 0;
  Predicate canHandle = nullptr;
  Handler handle = nullptr;
  void *userdata = nullptr;
  module_resources::OwnerHandle owner;
  module_resources::Registration lifetime;
};

struct Domain {
  std::string name;
  DomainMode mode = DomainMode::kExclusive;
  std::vector<Claim> order;
};

struct Registry {
  std::map<std::string, Domain, std::less<>> domains;
  std::map<Claim, Record> records;
  std::map<std::string, std::uint64_t, std::less<>> ownerSequences;
  Claim nextClaim = 1;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

CommandDomain CommandDomainFn = nullptr;
CommandBridge CommandBridgeFn = nullptr;

// Entry-point domains the framework owns. Commands are dynamic and created by
// the host provider; these are static and documented in events.h.
void ensureBuiltinDomains() {
  static const bool once = [] {
    registerDomain(ABCUI_CHANNEL_HUD_NATIVE_DRAW, DomainMode::kCollective);
    registerDomain(ABCUI_CHANNEL_NETMSG_SHOW_MENU, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_NETMSG_VOTE_MENU, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_TEXTFEED_SAY_TEXT, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_TEXTFEED_KILL_FEED, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_TEXTFEED_NOTICE, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_MENU_VGUI_MENU, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_MENU_COMMAND_KEY, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_SETTINGS_ESCAPE, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_INPUT_KEY, DomainMode::kExclusive);
    registerDomain(ABCUI_CHANNEL_INPUT_WHEEL, DomainMode::kExclusive);
    return true;
  }();
  (void)once;
}

bool roleValid(DomainMode mode, Role role) {
  if (mode == DomainMode::kCollective)
    return role == Role::kGate || role == Role::kObserver;
  return role == Role::kOwner || role == Role::kFallback ||
         role == Role::kObserver;
}

bool earlier(const Record &left, const Record &right) {
  if (left.priority != right.priority)
    return left.priority > right.priority;
  if (left.ownerId != right.ownerId)
    return left.ownerId < right.ownerId;
  return left.sequence < right.sequence;
}

void reorder(Domain &domain) {
  Registry &state = registry();
  std::sort(domain.order.begin(), domain.order.end(),
            [&state](Claim left, Claim right) {
              return earlier(state.records.at(left), state.records.at(right));
            });
}

struct Snapshot {
  Claim claim = 0;
  Role role = Role::kOwner;
  int priority = 0;
  Predicate canHandle = nullptr;
  Handler handle = nullptr;
  void *userdata = nullptr;
  module_resources::OwnerHandle owner;
};

std::vector<Snapshot> snapshot(const Domain &domain) {
  std::vector<Snapshot> result;
  result.reserve(domain.order.size());
  for (const Claim claim : domain.order) {
    const auto found = registry().records.find(claim);
    if (found == registry().records.end() || !found->second.active)
      continue;
    const Record &record = found->second;
    Snapshot entry;
    entry.claim = record.claim;
    entry.role = record.role;
    entry.priority = record.priority;
    entry.canHandle = record.canHandle;
    entry.handle = record.handle;
    entry.userdata = record.userdata;
    entry.owner = record.owner;
    result.push_back(std::move(entry));
  }
  return result;
}

bool matches(const Snapshot &entry, const void *event) {
  if (!entry.canHandle)
    return true;
  return module_resources::invoke(entry.owner, "claim predicate",
                                  entry.canHandle, entry.userdata, event) != 0;
}

void observe(const std::vector<Snapshot> &entries, const void *event) {
  for (const Snapshot &entry : entries) {
    if (entry.role != Role::kObserver)
      continue;
    if (entry.handle) {
      module_resources::invoke(entry.owner, "claim observer", entry.handle,
                               entry.userdata, event);
    } else if (entry.canHandle) {
      module_resources::invoke(entry.owner, "claim observer", entry.canHandle,
                               entry.userdata, event);
    }
  }
}

Decision pick(const std::vector<Snapshot> &entries, Role role,
              const void *event) {
  Decision decision;
  decision.role = role;
  int winningPriority = std::numeric_limits<int>::min();
  bool haveWinner = false;
  for (const Snapshot &entry : entries) {
    if (entry.role != role)
      continue;
    if (!matches(entry, event))
      continue;
    if (!haveWinner) {
      haveWinner = true;
      decision.claim = entry.claim;
      winningPriority = entry.priority;
      continue;
    }
    // Same-priority overlap is a design error: the bus must not fall back to
    // registration order to decide it. Higher-priority overlap is explicit.
    if (entry.priority == winningPriority)
      decision.conflict = true;
  }
  if (!haveWinner)
    decision.claim = 0;
  return decision;
}

} // namespace

bool registerDomain(const char *domain, DomainMode mode) {
  if (!domain || !*domain)
    return false;
  Registry &state = registry();
  const auto found = state.domains.find(domain);
  if (found != state.domains.end())
    return false;
  Domain created;
  created.name = domain;
  created.mode = mode;
  state.domains.emplace(domain, std::move(created));
  return true;
}

bool hasDomain(const char *domain) {
  ensureBuiltinDomains();
  return domain && registry().domains.contains(domain);
}

Claim claim(const char *domain, Role role, int priority, Predicate canHandle,
            Handler handle, void *userdata) {
  ensureBuiltinDomains();
  if (!domain || !*domain || !module_resources::accepting())
    return 0;
  Registry &state = registry();
  const auto found = state.domains.find(domain);
  if (found == state.domains.end() || !roleValid(found->second.mode, role))
    return 0;
  if (!canHandle && !handle)
    return 0;

  const module_resources::OwnerHandle owner = module_resources::current();
  const std::string ownerId = owner ? owner->id : std::string("<framework>");
  const Claim id = state.nextClaim++;
  Record record;
  record.claim = id;
  record.domain = found->first;
  record.ownerId = ownerId;
  record.role = role;
  record.priority = priority;
  record.sequence = ++state.ownerSequences[ownerId];
  record.canHandle = canHandle;
  record.handle = handle;
  record.userdata = userdata;
  record.owner = owner;
  record.lifetime.track([id] { release(id); });

  const auto [inserted, ok] = state.records.emplace(id, std::move(record));
  (void)ok;
  found->second.order.push_back(id);
  reorder(found->second);
  return id;
}

void release(Claim claimId) {
  if (!claimId)
    return;
  Registry &state = registry();
  const auto found = state.records.find(claimId);
  if (found == state.records.end())
    return;
  const std::string domain = found->second.domain;
  state.records.erase(found);
  const auto domainFound = state.domains.find(domain);
  if (domainFound == state.domains.end())
    return;
  std::erase(domainFound->second.order, claimId);
}

void setActive(Claim claimId, bool active) {
  if (!claimId)
    return;
  Registry &state = registry();
  const auto found = state.records.find(claimId);
  if (found != state.records.end())
    found->second.active = active;
}

Decision dispatch(const char *domain, const void *event) {
  ensureBuiltinDomains();
  if (!domain)
    return {};
  const auto found = registry().domains.find(domain);
  if (found == registry().domains.end() ||
      found->second.mode != DomainMode::kExclusive)
    return {};

  const std::vector<Snapshot> entries = snapshot(found->second);
  Decision owner = pick(entries, Role::kOwner, event);
  Decision fallback;
  if (!owner.claim)
    fallback = pick(entries, Role::kFallback, event);
  Decision &winner = owner.claim ? owner : fallback;

  Decision result;
  result.conflict = owner.conflict || fallback.conflict;
  if (winner.claim) {
    result.claim = winner.claim;
    result.role = winner.role;
    const auto record = registry().records.find(winner.claim);
    if (record != registry().records.end() && record->second.handle) {
      result.flags = module_resources::invoke(
          record->second.owner, "claim handler", record->second.handle,
          record->second.userdata, event);
      result.handled = (result.flags & kDecisionHandled) != 0;
    }
  }
  observe(entries, event);
  return result;
}

bool gate(const char *domain, const void *event) {
  ensureBuiltinDomains();
  if (!domain)
    return false;
  const auto found = registry().domains.find(domain);
  if (found == registry().domains.end() ||
      found->second.mode != DomainMode::kCollective)
    return false;
  for (const Snapshot &entry : snapshot(found->second)) {
    if (entry.role != Role::kGate)
      continue;
    if (matches(entry, event))
      return true;
  }
  return false;
}

void setCommandProvider(CommandDomain domain, CommandBridge bridge) {
  CommandDomainFn = domain;
  CommandBridgeFn = bridge;
}

Claim claimCommand(const char *name, Role role, int priority,
                   Predicate canHandle, Handler handle, void *userdata) {
  if (!name || !CommandDomainFn)
    return 0;
  const char *domain = CommandDomainFn(name);
  if (!domain || !*domain)
    return 0;
  registerDomain(domain, DomainMode::kExclusive);
  if (CommandBridgeFn)
    CommandBridgeFn(name);
  return claim(domain, role, priority, canHandle, handle, userdata);
}

std::vector<ClaimInfo> claims() {
  Registry &state = registry();
  std::vector<ClaimInfo> result;
  result.reserve(state.records.size());
  for (const auto &[id, record] : state.records) {
    ClaimInfo info;
    info.claim = id;
    info.domain = record.domain;
    info.owner = record.ownerId;
    info.role = record.role;
    info.priority = record.priority;
    info.active = record.active;
    info.sequence = record.sequence;
    result.push_back(std::move(info));
  }
  return result;
}

std::string domainList() {
  ensureBuiltinDomains();
  std::string result;
  for (const auto &[name, domain] : registry().domains) {
    if (!result.empty())
      result += ", ";
    result += name;
    result += domain.mode == DomainMode::kCollective ? "[]" : "";
  }
  return result;
}

} // namespace framework::arbitration
