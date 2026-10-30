// Offline acceptance for services::inventory::InventoryModel (issue 10).
// The wire semantics are pinned against the
// client's MsgFunc_InvAdd/MsgFunc_InvRemove and CInventoryPanel's action
// strings:
//   InvAdd: long id, byte canActivate, byte canDrop, byte removeLocked,
//           float value, five strings (name, displayName, description,
//           extra, key)
//   InvRemove: long id, byte force; id 0 clears every unlocked item and
//           force also drops removeLocked items.
//   Actions: activateitem "#<id>", dropitem "#<id>", dropitem "*".
#include "services/inventory/InventoryModel.h"

#include <iostream>
#include <string>
#include <vector>
#include "framework/TestHarness.h"

namespace {

using services::inventory::Action;
using services::inventory::InventoryModel;

netmsg::InvItemData makeItem(int id, const std::string &name,
                             const std::string &displayName,
                             const std::string &description) {
  netmsg::InvItemData data;
  data.id = id;
  data.canActivate = true;
  data.canDrop = true;
  data.value = 1.0f;
  data.name = name;
  data.displayName = displayName;
  data.description = description;
  data.extra = "extra";
  data.key = "key";
  return data;
}

void testAddOrderAndNames() {
  TEST_CASE("add-order-and-names");
  InventoryModel model;
  model.add(makeItem(7, "internal-seven", "Seven", "A seven"));
  model.add(makeItem(9, "internal-nine", "", "A nine"));
  const auto &items = model.items();
  CHECK(items.size() == 2);
  CHECK(items[0].id == 7 && items[0].name == "Seven");
  CHECK(items[1].id == 9 && items[1].name == "internal-nine"); // fallback
  CHECK(items[0].description == "A seven");
  CHECK(items[0].serial != items[1].serial);
  CHECK(model.revision() > 1);
}

void testSelectionAndCommands() {
  TEST_CASE("selection-and-commands");
  InventoryModel model;
  CHECK(!model.command(Action::kActivate).has_value());
  CHECK(!model.command(Action::kDrop).has_value());
  CHECK(!model.command(Action::kDropAll).has_value());

  model.add(makeItem(4, "four", "Four", ""));
  model.add(makeItem(5, "five", "", ""));

  // Keyboard walk selects the first row, then clamps at the ends.
  CHECK(model.moveSelection(1));
  CHECK(model.selected() && model.selected()->id == 4);
  CHECK(!model.moveSelection(-1));
  CHECK(model.moveSelection(1));
  CHECK(model.selected() && model.selected()->id == 5);
  CHECK(!model.moveSelection(1));

  // Commands carry the server id, not the model serial.
  CHECK(model.command(Action::kActivate) ==
        std::optional<std::string>("activateitem \"#5\""));
  CHECK(model.command(Action::kDrop) ==
        std::optional<std::string>("dropitem \"#5\""));
  CHECK(model.command(Action::kDropAll) ==
        std::optional<std::string>("dropitem \"*\""));

  // Unknown serial clears; same serial is a no-op.
  const std::uint64_t serial = model.selectedSerial();
  model.select(serial);
  CHECK(model.selectedSerial() == serial);
  model.select(123456);
  CHECK(model.selectedSerial() == 0 && !model.selected());
  CHECK(!model.command(Action::kActivate).has_value());
}

void testRemovalSemantics() {
  TEST_CASE("removal-semantics");
  InventoryModel model;
  model.add(makeItem(1, "one", "One", ""));
  model.add(makeItem(2, "two", "Two", ""));
  model.add(makeItem(1, "one-b", "One B", "")); // duplicate server id
  auto &items = model.items();
  CHECK(items.size() == 3);
  model.select(items[0].serial);

  // A non-matching id keeps the list and the selection.
  model.remove(99, 0);
  CHECK(model.items().size() == 3);
  CHECK(model.selected() && model.selected()->id == 1);

  // Removing the selected item clears the selection; the duplicate id is
  // removed too (the native rule matches on the server id).
  model.remove(1, 0);
  CHECK(model.items().size() == 1);
  CHECK(!model.selected());

  // id 0 removes every unlocked item.
  model.add(makeItem(3, "three", "Three", ""));
  model.remove(0, 0);
  CHECK(model.empty());

  // removeLocked: a normal removal keeps it, a forced one drops it.
  netmsg::InvItemData locked = makeItem(8, "eight", "Eight", "");
  locked.removeLocked = true;
  model.add(locked);
  model.remove(8, 0);
  CHECK(model.items().size() == 1);
  model.remove(0, 0);
  CHECK(model.items().size() == 1);
  model.remove(8, 1);
  CHECK(model.empty());
  model.remove(8, 1); // idempotent
  CHECK(model.empty());

  // clear() drops everything including the selection.
  model.add(makeItem(11, "eleven", "Eleven", ""));
  model.select(model.items().front().serial);
  model.clear();
  CHECK(model.empty() && model.selectedSerial() == 0);
}

void testRevisionAndNoOptimism() {
  TEST_CASE("revision-and-no-optimism");
  InventoryModel model;
  model.add(makeItem(21, "twenty-one", "Twenty One", ""));
  model.select(model.items().front().serial);
  const std::uint64_t before = model.revision();
  // Reading the command must not mutate the list (read-through).
  const auto command = model.command(Action::kDrop);
  CHECK(command.has_value());
  CHECK(model.items().size() == 1);
  CHECK(model.revision() == before);
  // Only the server's InvRemove empties the row.
  model.remove(21, 0);
  CHECK(model.empty());
  CHECK(model.revision() > before);
}

void testCap() {
  TEST_CASE("cap");
  InventoryModel model;
  for (int i = 0; i < 300; i++)
    model.add(makeItem(i, "item", "Item", ""));
  CHECK(model.items().size() == 256);
}

} // namespace

int main() {
  testAddOrderAndNames();
  testSelectionAndCommands();
  testRemovalSemantics();
  testRevisionAndNoOptimism();
  testCap();
  return abctest::finish("abc_ui_inventory_model");
}
