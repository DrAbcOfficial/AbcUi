// abc_ui_weapon_filter - the client-side weapon selector layout: override
// precedence, collision shifting inside the slot, clamping, order
// independence and the server-layout fallback. Pure placement rules, no
// engine and no registry.
#include "data/WeaponSlotLayout.h"
#include "framework/TestHarness.h"
#include "weaponfilter/src/WeaponFilterModel.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace weaponres;

namespace {

struct Entry {
  std::string name;
  int serverSlot = 0;
  int serverPos = 0;
  bool hasOverride = false;
  int overrideSlot = 0;
  int overridePos = 0;
};

std::vector<LayoutInput> inputsOf(const std::vector<Entry> &entries) {
  std::vector<LayoutInput> inputs;
  inputs.reserve(entries.size());
  for (const Entry &entry : entries) {
    inputs.push_back(LayoutInput{entry.name, entry.serverSlot, entry.serverPos,
                                 entry.hasOverride, entry.overrideSlot,
                                 entry.overridePos});
  }
  return inputs;
}

std::vector<LayoutResult> layoutOf(const std::vector<Entry> &entries) {
  const std::vector<LayoutInput> inputs = inputsOf(entries);
  const std::vector<LayoutResult> results = layoutWeapons(inputs);
  CHECK(results.size() == entries.size());
  return results;
}

int indexOf(const std::vector<Entry> &entries, std::string_view name) {
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].name == name)
      return static_cast<int>(i);
  return -1;
}

LayoutResult resultOf(const std::vector<Entry> &entries,
                      const std::vector<LayoutResult> &results,
                      std::string_view name) {
  const int index = indexOf(entries, name);
  if (index < 0 || static_cast<size_t>(index) >= results.size())
    return LayoutResult{-1, -1};
  return results[static_cast<size_t>(index)];
}

void checkPlacement(const std::vector<Entry> &entries, std::string_view name,
                    int slot, int pos) {
  const std::vector<LayoutResult> results = layoutOf(entries);
  const LayoutResult result = resultOf(entries, results, name);
  if (result.slot != slot || result.pos != pos)
    ::abctest::fail(__LINE__, "weapon placed on the expected slot/position");
}

std::string ruleJson(std::string_view name, int slot, int position) {
  return "{\"name\":\"" + std::string(name) +
         "\",\"slot\":" + std::to_string(slot) +
         ",\"position\":" + std::to_string(position) + "}";
}

std::string sectionJson(std::string_view weapons) {
  return "{\"version\":1,\"weapons\":[" + std::string(weapons) + "]}";
}

} // namespace

int main() {
  TEST_CASE("server-layout");
  {
    const std::vector<Entry> entries = {{"weapon_crowbar", 0, 0},
                                        {"weapon_shotgun", 2, 0},
                                        {"weapon_ins2coach", 3, 2},
                                        {"weapon_9mmhandgun", 1, 1}};
    const std::vector<LayoutResult> results = layoutOf(entries);
    CHECK(results.size() == 4);
    for (size_t i = 0; i < entries.size(); ++i) {
      CHECK(results[i].slot == entries[i].serverSlot);
      CHECK(results[i].pos == entries[i].serverPos);
    }
  }

  TEST_CASE("empty");
  {
    const std::vector<LayoutResult> results = layoutWeapons({});
    CHECK(results.empty());
  }

  TEST_CASE("override-moves");
  {
    const std::vector<Entry> entries = {{"weapon_crowbar", 0, 0},
                                        {"weapon_ins2coach", 3, 2, true, 5, 7}};
    checkPlacement(entries, "weapon_crowbar", 0, 0);
    checkPlacement(entries, "weapon_ins2coach", 5, 7);
  }

  TEST_CASE("override-claims-target");
  {
    // The rule takes 1/0; the server weapon that sat there shifts to the next
    // free position inside the same slot.
    const std::vector<Entry> entries = {{"weapon_server", 1, 0},
                                        {"weapon_rule", 5, 0, true, 1, 0}};
    checkPlacement(entries, "weapon_rule", 1, 0);
    checkPlacement(entries, "weapon_server", 1, 1);
  }

  TEST_CASE("collision-chain");
  {
    // Two server weapons at 0/0 and 0/1; the rule takes 0/0 and both shift.
    const std::vector<Entry> entries = {{"weapon_a", 0, 0},
                                        {"weapon_b", 0, 1},
                                        {"weapon_rule", 0, 0, true, 0, 0}};
    checkPlacement(entries, "weapon_rule", 0, 0);
    checkPlacement(entries, "weapon_a", 0, 1);
    checkPlacement(entries, "weapon_b", 0, 2);
  }

  TEST_CASE("equal-target-rules");
  {
    // Two rules on the same target: the name breaks the tie deterministically
    // and the second rule shifts one position.
    const std::vector<Entry> entries = {{"weapon_beta", 4, 0, true, 2, 4},
                                        {"weapon_alpha", 4, 1, true, 2, 4}};
    checkPlacement(entries, "weapon_alpha", 2, 4);
    checkPlacement(entries, "weapon_beta", 2, 5);
  }

  TEST_CASE("order-independent");
  {
    const std::vector<Entry> forward = {{"weapon_beta", 4, 0, true, 2, 4},
                                        {"weapon_alpha", 4, 1, true, 2, 4},
                                        {"weapon_gamma", 2, 4}};
    std::vector<Entry> backward(forward.rbegin(), forward.rend());
    const std::vector<LayoutResult> forwardResults = layoutOf(forward);
    const std::vector<LayoutResult> backwardResults = layoutOf(backward);
    for (const Entry &entry : forward) {
      const LayoutResult a = resultOf(forward, forwardResults, entry.name);
      const LayoutResult b = resultOf(backward, backwardResults, entry.name);
      CHECK(a.slot == b.slot);
      CHECK(a.pos == b.pos);
    }
  }

  TEST_CASE("clamped");
  {
    const std::vector<Entry> entries = {
        {"weapon_low", -3, -7}, {"weapon_high", 42, 999, true, 99, 1000}};
    checkPlacement(entries, "weapon_low", 0, 0);
    checkPlacement(entries, "weapon_high", kLayoutSlotCount - 1,
                   kLayoutMaxPosition);
  }

  TEST_CASE("full-slot-stays");
  {
    // A rule takes the first position of a completely full slot; the displaced
    // weapon stays in that slot at position 255 instead of moving elsewhere.
    std::vector<Entry> entries;
    entries.reserve(257);
    for (int pos = 0; pos <= kLayoutMaxPosition; ++pos) {
      entries.push_back(Entry{"weapon_" + std::to_string(pos), 0, pos});
    }
    entries.push_back(Entry{"weapon_rule", 0, 0, true, 0, 0});
    const std::vector<LayoutResult> results = layoutOf(entries);
    checkPlacement(entries, "weapon_rule", 0, 0);
    checkPlacement(entries, "weapon_0", 0, 1);
    checkPlacement(entries, "weapon_255", 0, kLayoutMaxPosition);
    for (const LayoutResult &result : results) {
      CHECK(result.slot == 0);
      CHECK(result.pos >= 0);
      CHECK(result.pos <= kLayoutMaxPosition);
    }
  }

  TEST_CASE("model-validation");
  CHECK(weaponfilter::validName("weapon_crowbar"));
  CHECK(
      weaponfilter::validName(std::string(weaponfilter::kMaxNameLength, 'a')));
  CHECK(!weaponfilter::validName(""));
  CHECK(!weaponfilter::validName(
      std::string(weaponfilter::kMaxNameLength + 1, 'a')));
  CHECK(!weaponfilter::validName("weapon crowbar"));
  CHECK(!weaponfilter::validName("weapon-crowbar"));
  CHECK(!weaponfilter::validName("weapon.crowbar"));
  CHECK(weaponfilter::validSlot(0));
  CHECK(weaponfilter::validSlot(9));
  CHECK(!weaponfilter::validSlot(-1));
  CHECK(!weaponfilter::validSlot(10));
  CHECK(weaponfilter::validPosition(0));
  CHECK(weaponfilter::validPosition(255));
  CHECK(!weaponfilter::validPosition(-1));
  CHECK(!weaponfilter::validPosition(256));

  TEST_CASE("model-parse");
  weaponfilter::Model model;
  std::string error;
  CHECK(model.parse(sectionJson(ruleJson("weapon_crowbar", 0, 0)), error));
  CHECK(model.rules().size() == 1);
  const weaponfilter::Rule *crowbar = model.find("weapon_crowbar");
  CHECK(crowbar != nullptr);
  if (crowbar) {
    CHECK(crowbar->slot == 0);
    CHECK(crowbar->position == 0);
  }
  // Unknown fields inside the section and inside a rule are ignored.
  CHECK(model.parse("{\"version\":1,\"note\":\"x\",\"weapons\":"
                    "[{\"name\":\"weapon_x\",\"slot\":1,"
                    "\"position\":2,\"note\":3}]}",
                    error));
  // A duplicate name keeps the last rule.
  CHECK(model.parse(sectionJson(ruleJson("weapon_a", 1, 1) + "," +
                                ruleJson("weapon_a", 2, 3)),
                    error));
  const weaponfilter::Rule *duplicate = model.find("weapon_a");
  CHECK(duplicate != nullptr);
  if (duplicate) {
    CHECK(duplicate->slot == 2);
    CHECK(duplicate->position == 3);
  }
  // A damaged section leaves the model unchanged.
  CHECK(!model.parse("{not json", error));
  CHECK(model.rules().size() == 1);
  CHECK(!model.parse("[]", error));
  CHECK(!model.parse("{\"weapons\":[]}", error));
  CHECK(!model.parse("{\"version\":2,\"weapons\":[]}", error));
  CHECK(!model.parse("{\"version\":1}", error));
  CHECK(!model.parse("{\"version\":1,\"weapons\":{}}", error));
  CHECK(!model.parse(sectionJson(ruleJson("weapon-a", 0, 0)), error));
  CHECK(!model.parse(sectionJson(ruleJson("weapon_a", 10, 0)), error));
  CHECK(!model.parse(sectionJson(ruleJson("weapon_a", 0, 256)), error));
  CHECK(!model.parse(sectionJson("{\"name\":\"weapon_a\",\"slot\":0}"), error));
  CHECK(model.rules().size() == 1);

  TEST_CASE("model-serialize");
  weaponfilter::Model ordered;
  CHECK(ordered.set(weaponfilter::Rule{"weapon_beta", 3, 2}, error));
  CHECK(ordered.set(weaponfilter::Rule{"weapon_alpha", 0, 1}, error));
  std::string json;
  CHECK(ordered.serialize(json, error));
  CHECK(json.find("weapon_alpha") < json.find("weapon_beta"));
  CHECK(json.find("\"version\":1") != std::string::npos);
  weaponfilter::Model roundTrip;
  CHECK(roundTrip.parse(json, error));
  CHECK(roundTrip.rules() == ordered.rules());

  TEST_CASE("model-limit");
  weaponfilter::Model limited;
  for (int i = 0; i < 256; ++i) {
    const weaponfilter::Rule rule{"weapon_" + std::to_string(i), 0, i};
    CHECK(limited.set(rule, error));
  }
  CHECK(limited.rules().size() == weaponfilter::kMaxRules);
  CHECK(!limited.set(weaponfilter::Rule{"weapon_extra", 0, 0}, error));
  // Replacing an existing rule is allowed at the cap.
  CHECK(limited.set(weaponfilter::Rule{"weapon_0", 3, 4}, error));
  const weaponfilter::Rule *replaced = limited.find("weapon_0");
  CHECK(replaced != nullptr);
  if (replaced)
    CHECK(replaced->slot == 3);
  CHECK(!limited.erase("weapon_missing"));
  CHECK(limited.erase("weapon_0"));
  CHECK(limited.set(weaponfilter::Rule{"weapon_extra", 0, 0}, error));
  limited.clear();
  CHECK(limited.rules().empty());

  return abctest::finish("abc_ui_weapon_filter");
}
