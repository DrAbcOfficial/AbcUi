// Contract test for the engine loading-plaque gate (issue #139).
//
// The engine reports level loading through two GameUI resource types:
// "level" (CL_ParseServerData; only when cl.maxclients > 1) and "transition"
// (SCR_BeginLoadingPlaque, which every changelevel sends before
// Host_ClearMemory). World-dependent reads (the player traces) must stay off
// while either bracket is open, independent of the connected-only hasLevel().
#include "services/loading/LevelLoadGate.h"

#include "framework/TestHarness.h"

int main() {
  TEST_CASE("level and transition are level-load resources");
  CHECK(services::loading::isLevelLoadResource("level"));
  CHECK(services::loading::isLevelLoadResource("transition"));
  CHECK(services::loading::isLevelLoadResource("LEVEL"));
  CHECK(services::loading::isLevelLoadResource("Transition"));

  TEST_CASE("other resources are not level loads");
  CHECK(!services::loading::isLevelLoadResource("model"));
  CHECK(!services::loading::isLevelLoadResource(""));
  CHECK(!services::loading::isLevelLoadResource(nullptr));

  TEST_CASE("the gate opens on start and closes on either end");
  services::loading::LevelLoadGate gate;
  CHECK(!gate.active());
  gate.started();
  CHECK(gate.active());
  gate.started(); // transition + level can both start; still one window
  CHECK(gate.active());
  gate.finished();
  CHECK(!gate.active());
  gate.started();
  gate.finished();
  CHECK(!gate.active());

  TEST_CASE("a disconnect closes the gate too");
  gate.started();
  gate.disconnected();
  CHECK(!gate.active());

  TEST_CASE("an end without a start is harmless");
  gate.finished();
  CHECK(!gate.active());

  return abctest::finish("abc_ui_level_load_gate");
}
