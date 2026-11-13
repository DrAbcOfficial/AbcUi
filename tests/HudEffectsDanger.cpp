// abc_ui_hud_effects - the abcui.hudeffects danger vignette driven through the
// recording HUD seams.
//
// Issue 137 (plan B): the vignette was only driven by the Health usermsg, so a
// revive that lost that sample kept the death-time red overlay until the next
// real health change. The panel now reconciles against the local client-data
// health every frame; these checks pin the low-health ramp, the immediate
// drop on a healthy sample with no usermsg, the issue-120 clamp, and the
// session/cvar gates.
#include <abcui/events.h>
#include <abcui/module_api.h>

#include "framework/HudModuleSeams.h"
#include "framework/TestHarness.h"

#include <cstring>
#include <string>

extern "C" int ABCUI_CALL AbcUiModule_Main(uint32_t, const AbcUiModuleApi *,
                                           AbcUiModuleDescriptor *);

namespace {

namespace seams = abctest::hudseams;

constexpr double kFrame = 1.0 / 60.0;

void sample(int health, int32_t spectate = 0, int32_t hideBits = 0) {
  seams::state().hud.suit = 1;
  seams::state().hud.spectate = spectate;
  seams::state().hud.hideBits = hideBits;
  seams::state().hud.health = health;
}

void frame() {
  const AbcUiPanelDesc &panel = seams::state().panel;
  CHECK(panel.onData != nullptr);
  if (panel.onData)
    panel.onData(panel.userdata, kFrame);
}

// Runs enough frames for the 0.2 s opacity ramp to land on its target.
void ramp() {
  for (int i = 0; i < 30; ++i)
    frame();
}

double opacity() {
  const std::string text = seams::elementProperty("danger", "opacity");
  return text.empty() ? -1.0 : std::stod(text);
}

void boot() {
  seams::reset();
  AbcUiModuleDescriptor &desc = seams::state().descriptor;
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &seams::api(), &desc) != 0);
  CHECK(desc.id != nullptr && std::strcmp(desc.id, "abcui.hudeffects") == 0);
  CHECK(desc.version != nullptr && std::strcmp(desc.version, "3.0.1") == 0);
  if (desc.onLoad)
    desc.onLoad();
  if (desc.onInit)
    desc.onInit();

  const AbcUiPanelDesc *panel = seams::state().registered;
  CHECK(panel != nullptr);
  if (!panel)
    return;
  CHECK(std::strcmp(panel->name, "effect") == 0);
  CHECK(std::strcmp(panel->documentPath,
                    "mod://abcui.hudeffects/hud/rml/effect.rml") == 0);
  CHECK(panel->onData != nullptr);
  CHECK(panel->reset != nullptr);
  // Session start: the host shows the panel and clears every overlay.
  if (panel->reset)
    panel->reset(panel->userdata);
}

} // namespace

int main() {
  TEST_CASE("contract");
  boot();
  CHECK(seams::elementProperty("danger", "display") == "none");

  TEST_CASE("low-health-ramps-the-vignette");
  sample(10);
  frame();
  CHECK(seams::elementProperty("danger", "display") == "block");
  ramp();
  CHECK_NEAR(opacity(), 1.0 - 10.0 / 45.0, 0.01);

  TEST_CASE("revive-reconciles-without-a-usermsg");
  // The player is healthy again and no Health message reached the panel: the
  // reconcile must drop the overlay on the same frame.
  sample(50);
  frame();
  CHECK(seams::elementProperty("danger", "display") == "none");
  CHECK(opacity() == 0.0);

  TEST_CASE("non-positive-health-clamps-to-one");
  sample(0); // dead, connected, not spectating
  frame();
  CHECK(seams::elementProperty("danger", "display") == "block");
  ramp();
  CHECK_NEAR(opacity(), 1.0 - 1.0 / 45.0, 0.01);

  TEST_CASE("usermsg-stays-immediate");
  sample(50);
  ramp(); // settle back to hidden
  seams::publishHealth(10);
  CHECK(seams::elementProperty("danger", "display") == "block");
  frame();
  // The next frame reconciles to the authoritative client-data health.
  CHECK(seams::elementProperty("danger", "display") == "none");

  TEST_CASE("spectate-hides-the-panel");
  sample(10, 1);
  frame();
  CHECK(seams::state().visible == false);

  TEST_CASE("hide-all-token-hides-the-panel");
  sample(10, 0, ABCUI_HIDEBIT_ALL);
  frame();
  CHECK(seams::state().visible == false);

  TEST_CASE("hud-efx-off-hides-the-panel");
  seams::state().cvarValue = 0.0f;
  sample(10);
  frame();
  CHECK(seams::state().visible == false);

  TEST_CASE("missing-local-player-hides-the-panel");
  seams::state().cvarValue = 45.0f;
  seams::state().playerIndex = 0;
  sample(10);
  frame();
  CHECK(seams::state().visible == false);

  TEST_CASE("active-panel-shows-again");
  seams::state().playerIndex = 1;
  sample(50);
  frame();
  CHECK(seams::state().visible == true);

  TEST_CASE("unload");
  AbcUiModuleDescriptor &desc = seams::state().descriptor;
  if (desc.onUnload)
    desc.onUnload();

  return abctest::finish("abc_ui_hud_effects");
}
