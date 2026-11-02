// framework::facts implementation - see Facts.h.
#include "Facts.h"

#include <cstring>
#include <string_view>

namespace framework::facts {
namespace {

MenuState Menu;

std::uint32_t bump(std::uint32_t value) {
  return value == 0 ? 1 : value + 1;
}

} // namespace

void openMenu(std::int32_t validSlots, std::int32_t displaySeconds,
              std::int32_t bits, double now) {
  if (validSlots <= 0) {
    clearMenu();
    return;
  }
  Menu.visible = true;
  Menu.validSlots = validSlots;
  Menu.ammoMenu = (bits >> 7) & 1;
  Menu.deadline = displaySeconds > 0 ? now + displaySeconds : -1.0;
  Menu.generation = bump(Menu.generation);
}

void clearMenu() {
  if (!Menu.visible && Menu.validSlots == 0)
    return;
  const std::uint32_t next = bump(Menu.generation);
  Menu = MenuState{};
  Menu.generation = next;
}

void expire(double now) {
  if (!Menu.visible || Menu.deadline < 0.0 || now < Menu.deadline)
    return;
  clearMenu();
}

void reset() {
  clearMenu();
}

const MenuState &menu() {
  return Menu;
}

bool get(const char *name, void *out, std::uint32_t outSize) {
  if (!name || !out)
    return false;
  if (std::string_view(name) != ABCUI_FACT_MENU)
    return false;
  if (outSize < sizeof(AbcUiMenuFacts))
    return false;
  AbcUiMenuFacts value{};
  value.structSize = sizeof(value);
  value.visible = Menu.visible ? 1 : 0;
  value.validSlots = Menu.validSlots;
  value.ammoMenu = Menu.ammoMenu;
  value.generation = Menu.generation;
  std::memcpy(out, &value, sizeof(value));
  return true;
}

std::uint32_t generation(const char *name) {
  if (name && std::string_view(name) == ABCUI_FACT_MENU)
    return Menu.generation;
  return 0;
}

} // namespace framework::facts
