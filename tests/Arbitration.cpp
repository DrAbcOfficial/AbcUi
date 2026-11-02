// Entry-point arbitration: ownership, order independence, roles, gates and
// the framework menu fact. Links the production engine-free implementation.
#include "framework/TestHarness.h"
#include "modsys/Arbitration.h"
#include "modsys/Facts.h"
#include "modsys/ModuleResources.h"

#include <memory>
#include <string>
#include <vector>

namespace framework::module_resources {
void reportException(const char *, const char *) {}
} // namespace framework::module_resources

namespace {

namespace facts = framework::facts;

using framework::arbitration::Claim;
using framework::arbitration::Decision;
using framework::arbitration::DomainMode;
using framework::arbitration::Role;

std::vector<std::string> Log;

int always(void *, const void *) {
  return 1;
}

int never(void *, const void *) {
  return 0;
}

int32_t handleA(void *, const void *) {
  Log.emplace_back("a");
  return framework::arbitration::kDecisionHandled;
}

int32_t handleB(void *, const void *) {
  Log.emplace_back("b");
  return framework::arbitration::kDecisionHandled;
}

int32_t observe(void *, const void *) {
  Log.emplace_back("observer");
  return 0;
}

int gateOne(void *, const void *event) {
  return *static_cast<const int *>(event) == 1;
}

int gateTwo(void *, const void *event) {
  return *static_cast<const int *>(event) == 2;
}

Claim claimAs(const std::string &owner, const char *domain, Role role,
              int priority, framework::arbitration::Predicate can,
              framework::arbitration::Handler handle) {
  auto scope = std::make_shared<framework::module_resources::Owner>(owner);
  framework::module_resources::Scope current(scope);
  return framework::arbitration::claim(domain, role, priority, can, handle,
                                       nullptr);
}

} // namespace

int main() {
  using namespace framework::arbitration;

  TEST_CASE("register the domains");
  CHECK(registerDomain("test.exclusive", DomainMode::kExclusive));
  CHECK(!registerDomain("test.exclusive", DomainMode::kExclusive));
  CHECK(registerDomain("test.collective", DomainMode::kCollective));
  CHECK(hasDomain("test.exclusive"));

  TEST_CASE("owners win by explicit priority and stable owner id");
  // Same priority: the lexicographically smaller owner id wins regardless of
  // registration order, and the overlap is diagnosed.
  Log.clear();
  Claim b = claimAs("b", "test.exclusive", Role::kOwner, 0, always, handleB);
  Claim a = claimAs("a", "test.exclusive", Role::kOwner, 0, always, handleA);
  CHECK(a != 0 && b != 0);
  Decision decision = dispatch("test.exclusive", nullptr);
  CHECK(decision.handled);
  CHECK(decision.conflict);
  CHECK(decision.claim == a);
  CHECK(Log.size() == 1 && Log[0] == "a");
  release(a);
  release(b);

  TEST_CASE("higher priority overlap is explicit, not a conflict");
  Log.clear();
  Claim high =
      claimAs("a", "test.exclusive", Role::kOwner, 10, always, handleA);
  Claim low = claimAs("b", "test.exclusive", Role::kOwner, 0, always, handleB);
  decision = dispatch("test.exclusive", nullptr);
  CHECK(decision.handled && !decision.conflict);
  CHECK(Log.size() == 1 && Log[0] == "a");
  release(low);
  release(high);

  TEST_CASE("fallback runs only when no owner matches");
  Log.clear();
  Claim fallback =
      claimAs("c", "test.exclusive", Role::kFallback, 0, always, handleB);
  decision = dispatch("test.exclusive", nullptr);
  CHECK(decision.handled);
  CHECK(decision.role == Role::kFallback);
  CHECK(Log.size() == 1 && Log[0] == "b");
  release(fallback);
  // A long-lived base owner for the observer/activation cases below.
  Claim base = claimAs("a", "test.exclusive", Role::kOwner, 0, always, handleA);

  TEST_CASE("observers run after the decision and never change it");
  Log.clear();
  Claim observer =
      claimAs("d", "test.exclusive", Role::kObserver, 0, nullptr, observe);
  decision = dispatch("test.exclusive", nullptr);
  CHECK(decision.handled);
  CHECK(Log.size() == 2 && Log[0] == "a" && Log[1] == "observer");
  release(observer);

  TEST_CASE("setActive stops a claim from participating");
  Claim inactive =
      claimAs("e", "test.exclusive", Role::kOwner, 100, always, handleB);
  setActive(inactive, false);
  Log.clear();
  decision = dispatch("test.exclusive", nullptr);
  CHECK(decision.claim != inactive);
  CHECK(Log.size() == 1 && Log[0] == "a");
  release(inactive);
  release(base);

  TEST_CASE("collective gates are an any-true query");
  claimAs("f", "test.collective", Role::kGate, 0, gateOne, nullptr);
  claimAs("g", "test.collective", Role::kGate, 0, gateTwo, nullptr);
  const int one = 1;
  const int two = 2;
  const int three = 3;
  CHECK(gate("test.collective", &one));
  CHECK(gate("test.collective", &two));
  CHECK(!gate("test.collective", &three));
  CHECK(!gate("test.exclusive", &one));

  TEST_CASE("the menu fact is monotonic and deadline-aware");
  facts::openMenu(0x7, 15, 128, 100.0);
  CHECK(facts::menu().visible);
  CHECK(facts::menu().validSlots == 0x7);
  CHECK(facts::menu().ammoMenu == 1);
  AbcUiMenuFacts snapshot{};
  snapshot.structSize = sizeof(snapshot);
  CHECK(facts::get(ABCUI_FACT_MENU, &snapshot, sizeof(snapshot)));
  CHECK(snapshot.visible == 1 && snapshot.validSlots == 0x7);
  CHECK(!facts::get("unknown", &snapshot, sizeof(snapshot)));
  const uint32_t generation = facts::generation(ABCUI_FACT_MENU);
  facts::clearMenu();
  CHECK(!facts::menu().visible);
  CHECK(facts::generation(ABCUI_FACT_MENU) != generation);
  facts::openMenu(0x3, 5, 0, 100.0);
  facts::expire(200.0);
  CHECK(!facts::menu().visible);

  TEST_CASE("commands claim through the host provider");
  setCommandProvider(
      [](const char *name) {
        static std::string domain;
        domain = std::string("command.") + name;
        return domain.c_str();
      },
      [](const char *) { return true; });
  const Claim command =
      claimCommand("slot1", Role::kOwner, 0, always, handleA, nullptr);
  CHECK(command != 0);
  CHECK(hasDomain("command.slot1"));
  Log.clear();
  decision = dispatch("command.slot1", nullptr);
  CHECK(decision.handled && Log.size() == 1 && Log[0] == "a");

  return abctest::finish("arbitration");
}
