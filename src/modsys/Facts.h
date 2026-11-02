#pragma once

// framework::facts - typed, read-only facts derived by the framework from
// the wire or the host state. Modules never write a framework fact; their
// own UI state stays private and is observed through claims.
//
// The first fact is the text-menu input state. It is updated from the
// ShowMenu stream (not by the text-menu module), so a missing or failed
// renderer never changes whether the number keys belong to a menu.

#include <cstdint>

#include <abcui/module_api.h>

namespace framework::facts {

struct MenuState {
  bool visible = false;
  std::int32_t validSlots = 0;
  std::int32_t ammoMenu = 0;
  std::uint32_t generation = 0;
  double deadline = -1.0;
};

// A complete ShowMenu replaces the state; validSlots == 0 closes it.
void openMenu(std::int32_t validSlots, std::int32_t displaySeconds,
              std::int32_t bits, double now);
void clearMenu();
// Deadline expiry for menus without their own module timeout (the native
// fallback path). Called once per frame with the client clock.
void expire(double now);
void reset();

const MenuState &menu();

// Named, size-negotiated snapshots. Returns false for an unknown name or a
// caller record smaller than the fact.
bool get(const char *name, void *out, std::uint32_t outSize);
std::uint32_t generation(const char *name);

} // namespace framework::facts
