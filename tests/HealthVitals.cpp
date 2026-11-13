// abc_ui_health_vitals - the abcui.health readout driven through the recording
// HUD seams.
//
// Issue 137: the number was only written from the Health usermsg, so a revive
// whose sample was consumed while the readout was still gated kept the death
// value on screen until the next real health change. The panel now reconciles
// against the local client-data health every frame; these checks pin that the
// reconcile wins without any usermsg, that the usermsg stays an immediate path,
// that the issue-120 non-positive clamp still renders 1, and that the bar
// tracker still suppresses a settled readout.
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

void sample(int health, bool suit = true, int32_t spectate = 0,
            int32_t hideBits = 0) {
  seams::state().hud.suit = suit ? 1 : 0;
  seams::state().hud.spectate = spectate;
  seams::state().hud.hideBits = hideBits;
  seams::state().hud.health = health;
}

// One HUD_Frame pump for the registered panel.
void frame(double seconds = 1.0 / 60.0) {
  const AbcUiPanelDesc &panel = seams::state().panel;
  CHECK(panel.onData != nullptr);
  if (panel.onData)
    panel.onData(panel.userdata, seconds);
}

// Lets a running value pop settle, so a "no write" check cannot trip over the
// burst the previous change started.
void settle() { frame(1.0); }

void documentReady() {
  const AbcUiPanelDesc &panel = seams::state().panel;
  CHECK(panel.onDocumentReady != nullptr);
  if (panel.onDocumentReady)
    panel.onDocumentReady(panel.userdata);
}

void boot() {
  seams::reset();
  AbcUiModuleDescriptor &desc = seams::state().descriptor;
  CHECK(AbcUiModule_Main(ABCUI_ABI_VERSION, &seams::api(), &desc) != 0);
  CHECK(desc.id != nullptr && std::strcmp(desc.id, "abcui.health") == 0);
  CHECK(desc.version != nullptr && std::strcmp(desc.version, "3.0.1") == 0);
  if (desc.onLoad)
    desc.onLoad();
  if (desc.onInit)
    desc.onInit();

  const AbcUiPanelDesc *panel = seams::state().registered;
  CHECK(panel != nullptr);
  if (!panel)
    return;
  CHECK(std::strcmp(panel->name, "health") == 0);
  CHECK(std::strcmp(panel->documentPath,
                    "mod://abcui.health/hud/rml/health.rml") == 0);
  CHECK(panel->onData != nullptr);
  CHECK(panel->onDocumentReady != nullptr);
  // The module claims the native HUD elements it replaces.
  CHECK(seams::state().claims == 1);
  CHECK(seams::state().claimDomain == ABCUI_CHANNEL_HUD_NATIVE_DRAW);
  CHECK(!seams::state().subscriptions.empty());
}

} // namespace

int main() {
  TEST_CASE("contract");
  boot();

  TEST_CASE("document-ready-seeds-the-snapshot");
  sample(0); // dead and not spectating: the issue-120 clamp
  documentReady();
  CHECK(seams::elementText("health-label") == "1");
  CHECK(!seams::elementProperty("health-bar", "background-color").empty());

  TEST_CASE("revive-reconciles-without-a-usermsg");
  // The server revived the player; no Health message reached the panel.
  sample(50);
  const int textWritesBeforeRevive = seams::state().textWrites;
  frame();
  CHECK(seams::elementText("health-label") == "50");
  CHECK(seams::state().textWrites == textWritesBeforeRevive + 1);

  TEST_CASE("settled-readout-does-not-write");
  settle();
  const int textWrites = seams::state().textWrites;
  const int classWrites = seams::state().classWrites;
  frame();
  frame();
  CHECK(seams::state().textWrites == textWrites);
  CHECK(seams::state().classWrites == classWrites);

  TEST_CASE("spectate-gate");
  sample(50, true, 1);
  settle();
  const int textWritesWhileShown = seams::state().textWrites;
  frame();
  CHECK(seams::state().textWrites == textWritesWhileShown);
  CHECK(seams::elementText("health-label") == "50");

  TEST_CASE("hidden-health-group");
  sample(50, true, 0, ABCUI_HIDEBIT_HEALTH);
  settle();
  const int textWritesWhileHidden = seams::state().textWrites;
  frame();
  CHECK(seams::state().textWrites == textWritesWhileHidden);
  // The group comes back: the tracker snaps and writes the current value.
  sample(50);
  frame();
  CHECK(seams::elementText("health-label") == "50");
  CHECK(seams::state().textWrites == textWritesWhileHidden + 1);

  TEST_CASE("usermsg-stays-immediate");
  sample(50);
  settle();
  seams::publishHealth(30);
  CHECK(seams::elementText("health-label") == "30");
  // The next frame reconciles to the authoritative client-data value.
  frame();
  CHECK(seams::elementText("health-label") == "50");

  TEST_CASE("non-positive-usermsg-still-clamps");
  seams::publishHealth(0);
  CHECK(seams::elementText("health-label") == "1");
  frame();
  CHECK(seams::elementText("health-label") == "50");

  TEST_CASE("missing-local-player-does-not-clamp");
  seams::state().playerIndex = 0;
  sample(0);
  frame();
  CHECK(seams::elementText("health-label") == "0");
  seams::state().playerIndex = 1;
  frame();
  CHECK(seams::elementText("health-label") == "1");

  TEST_CASE("unload");
  AbcUiModuleDescriptor &desc = seams::state().descriptor;
  if (desc.onUnload)
    desc.onUnload();

  return abctest::finish("abc_ui_health_vitals");
}
