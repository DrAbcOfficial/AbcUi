// abc_ui_menu_state - T04 acceptance for the production menu state machine.
// Links the real MenuController.cpp (no model copy); the backend seam is a
// recording fake of services::IGameUiBackend. Table-driven: every case is a
// sequence of steps, and every step asserts the exact phase/page/overlay
// snapshot, the exact backend action log delta, the connect generation and
// (where set) the error reason. After every step the derived outputs are
// re-checked: isMenuActive == hasExclusiveInput, the int/bool conversions
// agree, pointer target follows menu presence and canCloseMenu is
// InGame-only (hook-contract.md §4: the shipped GameUI forwards one query to
// the other, so the replacement keeps them equal by construction).
#include "modules/gameui/navigation/MenuController.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "framework/TestHarness.h"

namespace {

class RecordingBackend final : public services::IGameUiBackend {
public:
  std::vector<std::string> Log;

  void connect(uint32_t generation, const char *address) override {
    Log.push_back("connect:" + std::to_string(generation) + ":" + address);
  }
  void startLocalGame(uint32_t generation, const char *map) override {
    Log.push_back("startLocalGame:" + std::to_string(generation) + ":" + map);
  }
  void cancelConnect(uint32_t generation) override {
    Log.push_back("cancelConnect:" + std::to_string(generation));
  }
  void submitPassword(uint32_t generation, const char *password) override {
    Log.push_back("submitPassword:" + std::to_string(generation) + ":" +
                  password);
  }
  void disconnectFromGame() override {
    Log.push_back("disconnect");
  }
  void quitGame() override {
    Log.push_back("quit");
  }
  void resumeGame() override {
    Log.push_back("resume");
  }
};

using gameui::menu::BackendPhase;
using gameui::menu::EventKind;
using gameui::menu::EventOrigin;
using gameui::menu::LoadingOutcome;
using gameui::menu::MenuController;
using gameui::menu::MenuEvent;
using gameui::menu::MenuOverlay;
using gameui::menu::MenuPage;
using gameui::menu::MenuState;
using gameui::menu::TextInputTarget;

// One step: a burst of events, then the exact expected snapshot. The action
// list is the exact delta the backend log must have grown by.
struct Step {
  std::vector<MenuEvent> events;
  BackendPhase phase = BackendPhase::kBooting;
  std::vector<MenuPage> pages;
  std::vector<MenuOverlay> overlays;
  std::vector<std::string> actions;
  const char *errorReason = nullptr; // checked only when non-null
};

struct Case {
  const char *name;
  std::vector<Step> steps;
};

std::vector<MenuPage> pagesOf(const MenuState &state) {
  return {state.pages.begin(), state.pages.end()};
}

std::vector<MenuOverlay> overlaysOf(const MenuState &state) {
  return {state.overlays.begin(), state.overlays.end()};
}

void runStep(const Step &step, MenuController &controller,
             RecordingBackend &backend) {
  const size_t logStart = backend.Log.size();
  for (const MenuEvent &event : step.events)
    controller.dispatch(event);
  const MenuState &state = controller.state();

  CHECK(state.phase == step.phase);
  CHECK(pagesOf(state) == step.pages);
  CHECK(overlaysOf(state) == step.overlays);
  CHECK(backend.Log.size() - logStart == step.actions.size());
  for (size_t i = 0;
       i < step.actions.size() && logStart + i < backend.Log.size(); i++)
    CHECK(backend.Log[logStart + i] == step.actions[i]);
  if (step.errorReason)
    CHECK(state.errorReason == step.errorReason);

  // Derived-output invariants, every step without exception.
  const bool active = controller.isMenuActive();
  CHECK(controller.hasExclusiveInput() == active);
  CHECK(controller.activationFlag() == (active ? 1 : 0));
  CHECK((controller.pointerTarget() == gameui::menu::PointerTarget::kMenu) ==
        active);
  CHECK(controller.canCloseMenu() == (state.phase == BackendPhase::kInGame));
  CHECK(active == (!step.pages.empty() || !step.overlays.empty()));
}

void runCase(const Case &testCase) {
  TEST_CASE(testCase.name);
  TEST_STEP(0);
  RecordingBackend backend;
  MenuController controller(&backend);
  int stepNumber = 0;
  for (const Step &step : testCase.steps) {
    TEST_STEP(++stepNumber);
    runStep(step, controller, backend);
  }
}

// Reusable arrange step: boot reported done (session 1), then a full
// generation-1 connect that succeeds and closes the menu (in game).
Step enterGame(const char *address) {
  Step step;
  step.events = {
      gameui::menu::makePhaseChanged(1, BackendPhase::kDisconnected),
      gameui::menu::makeConnectRequest(address),
      gameui::menu::makeLoadingStarted(1),
      gameui::menu::makeLoadingFinished(1, LoadingOutcome::kSuccess, nullptr),
  };
  step.phase = BackendPhase::kInGame;
  step.actions = {std::string("connect:1:") + address};
  return step;
}

void runStandaloneChecks() {
  TEST_CASE("standalone");
  TEST_STEP(0);

  // Initial state: booting, nothing presented, game input, no actions.
  {
    TEST_STEP(1);
    RecordingBackend backend;
    MenuController controller(&backend);
    CHECK(controller.state().phase == BackendPhase::kBooting);
    CHECK(!controller.isMenuActive());
    CHECK(!controller.hasExclusiveInput());
    CHECK(controller.activationFlag() == 0);
    CHECK(controller.pointerTarget() == gameui::menu::PointerTarget::kGame);
    CHECK(controller.textInputTarget() == TextInputTarget::kNone);
    CHECK(backend.Log.empty());

    // Page opens before the menu exists are dropped.
    controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kSettings));
    CHECK(!controller.isMenuActive());

    // Open-page during a live attempt is refused: Loading holds input.
    controller.dispatch(
        gameui::menu::makePhaseChanged(1, BackendPhase::kDisconnected));
    controller.dispatch(gameui::menu::makeConnectRequest("c.example:1"));
    CHECK(controller.state().phase == BackendPhase::kConnecting);
    controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kServers));
    CHECK(pagesOf(controller.state()) ==
          std::vector<MenuPage>{MenuPage::kLoading});

    // Text targets follow the top text-capable overlay.
    controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
    CHECK(controller.textInputTarget() == TextInputTarget::kConsole);
    controller.dispatch(gameui::menu::makeLoadingFinished(
        1, LoadingOutcome::kPasswordRequired, nullptr));
    CHECK((overlaysOf(controller.state()) ==
           std::vector<MenuOverlay>{MenuOverlay::kConsole,
                                    MenuOverlay::kPassword}));
    CHECK(controller.textInputTarget() == TextInputTarget::kPasswordField);
  }

  // Null backend: the model still transitions, actions are dropped, nothing
  // crashes (presentation-only wiring shape).
  {
    TEST_STEP(2);
    MenuController controller(nullptr);
    controller.dispatch(
        gameui::menu::makePhaseChanged(1, BackendPhase::kDisconnected));
    controller.dispatch(gameui::menu::makeConnectRequest("n.example:1"));
    CHECK(controller.state().phase == BackendPhase::kConnecting);
    controller.dispatch(MenuEvent{EventKind::kConnectCancel});
    CHECK(controller.state().phase == BackendPhase::kDisconnected);
    controller.dispatch(
        gameui::menu::makePhaseChanged(2, BackendPhase::kInGame));
    controller.dispatch(MenuEvent{EventKind::kMenuToggle});
    CHECK(pagesOf(controller.state()) ==
          std::vector<MenuPage>{MenuPage::kPause});
    controller.dispatch(MenuEvent{EventKind::kMenuToggle});
    CHECK(controller.state().pages.empty());
    controller.dispatch(MenuEvent{EventKind::kMenuToggle});
    controller.dispatch(MenuEvent{EventKind::kQuitRequested});
    controller.dispatch(gameui::menu::makeQuitResolved(true));
    CHECK(controller.state().overlays.empty());
    CHECK(controller.state().phase == BackendPhase::kInGame);
  }

  // Console hotkey close: only the plain in-game pause page with the
  // console as its sole overlay returns to gameplay. The no-map home,
  // deeper pages and other modals keep their context (the console layer
  // alone closes there).
  {
    TEST_STEP(3);
    MenuController controller(nullptr);
    controller.dispatch(
        gameui::menu::makePhaseChanged(1, BackendPhase::kDisconnected));
    CHECK(!controller.state().consoleHotkeyClosesMenu()); // no-map Main
    controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
    CHECK(!controller.state().consoleHotkeyClosesMenu());

    controller.dispatch(
        gameui::menu::makePhaseChanged(2, BackendPhase::kInGame));
    controller.dispatch(MenuEvent{EventKind::kMenuToggle}); // Pause
    controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
    CHECK(controller.state().consoleHotkeyClosesMenu());
    controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kSettings));
    CHECK(!controller.state().consoleHotkeyClosesMenu()); // deeper page

    MenuController modal(nullptr);
    modal.dispatch(gameui::menu::makePhaseChanged(1, BackendPhase::kInGame));
    modal.dispatch(MenuEvent{EventKind::kMenuToggle}); // Pause
    modal.dispatch(MenuEvent{EventKind::kQuitRequested});
    modal.dispatch(MenuEvent{EventKind::kConsoleToggle});
    CHECK(!modal.state().consoleHotkeyClosesMenu()); // modal underneath
  }

  // Password strings and addresses are value copies: the controller keeps
  // its own storage, the caller's buffers can die immediately.
  {
    TEST_STEP(3);
    RecordingBackend backend;
    MenuController controller(&backend);
    controller.dispatch(
        gameui::menu::makePhaseChanged(1, BackendPhase::kDisconnected));
    {
      char address[32];
      sprintf_s(address, "copy.example:%d", 1234);
      controller.dispatch(gameui::menu::makeConnectRequest(address));
    }
    CHECK(controller.state().connectAddress == "copy.example:1234");
    CHECK(backend.Log.size() == 1);
    CHECK(backend.Log[0] == "connect:1:copy.example:1234");
  }
}

} // namespace

int main() {
  // -- table part: one row per frozen transition-group under test ---------
  const Case cases[] = {
      {
          // No-map home: repeated ESC can never make it vanish, zero actions.
          "home-no-map-esc",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // In-game ESC is a single toggle: Pause <-> closed, one resume per
          // close.
          "game-pause-toggle",
          {
              enterGame("10.0.0.7:27015"),
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .actions = {"resume"},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
          },
      },
      {
          // Settings opened from Main returns to Main; the home survives back.
          "settings-return-from-main",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeOpenPage(MenuPage::kSettings)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain, MenuPage::kSettings},
              },
              {
                  .events = {gameui::menu::makeOpenPage(MenuPage::kServers)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain, MenuPage::kSettings,
                            MenuPage::kServers},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain, MenuPage::kSettings},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Settings opened from Pause returns to Pause, not to the no-map
          // home.
          "settings-return-from-pause",
          {
              enterGame("10.0.0.7:27015"),
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  .events = {gameui::menu::makeOpenPage(MenuPage::kSettings)},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause, MenuPage::kSettings},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .actions = {"resume"},
              },
          },
      },
      {
          // Console wraps modals: ESC pops exactly one layer and never hands
          // input back to the game while the no-map home is up.
          "console-over-modal",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kConsoleToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConsole},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kQuitRequested}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConfirmQuit},
              },
              {
                  // Repeated quit request: still one confirm, not a stack.
                  .events = {MenuEvent{EventKind::kQuitRequested}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConfirmQuit},
              },
              {
                  .events = {MenuEvent{EventKind::kConsoleToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConfirmQuit,
                               MenuOverlay::kConsole},
              },
              {
                  // One ESC = the console only; the confirm underneath stays.
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConfirmQuit},
              },
              {
                  .events = {gameui::menu::makeQuitResolved(false)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Native dual-fire: activation is idempotent, hide refuses no-map,
          // sequence tags drop re-deliveries, untagged events are a wiring
          // bug and are dropped. No repeated-toggle cancellation anywhere.
          "native-dedupe",
          {
              {
                  .events =
                      {
                          gameui::menu::makeNative(EventKind::kNativeActivate,
                                                   EventOrigin::kNone, 5),
                          gameui::menu::makeNative(EventKind::kNativeActivate,
                                                   EventOrigin::kBaseUi, 0),
                      },
                  .phase = BackendPhase::kBooting,
              },
              {
                  .events = {gameui::menu::makeNative(
                      EventKind::kNativeActivate, EventOrigin::kBaseUi, 1)},
                  .phase = BackendPhase::kBooting,
                  .pages = {MenuPage::kMain},
              },
              {
                  // The GameUI-inner layer reports the same logical activation:
                  // idempotent open, NOT a second toggle.
                  .events = {gameui::menu::makeNative(
                      EventKind::kNativeActivate, EventOrigin::kGameUi, 1)},
                  .phase = BackendPhase::kBooting,
                  .pages = {MenuPage::kMain},
              },
              {
                  // Exact re-delivery from the same layer: dropped by sequence.
                  .events = {gameui::menu::makeNative(
                      EventKind::kNativeActivate, EventOrigin::kBaseUi, 1)},
                  .phase = BackendPhase::kBooting,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makePhaseChanged(
                      2, BackendPhase::kInGame)},
                  .phase = BackendPhase::kInGame,
              },
              {
                  // Dual-fired activation opens Pause exactly once.
                  .events =
                      {
                          gameui::menu::makeNative(EventKind::kNativeActivate,
                                                   EventOrigin::kBaseUi, 2),
                          gameui::menu::makeNative(EventKind::kNativeActivate,
                                                   EventOrigin::kGameUi, 2),
                      },
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  // Engine-initiated hide closes WITHOUT resumeGame(); the
                  // per-origin counter continues after the activations.
                  .events =
                      {
                          gameui::menu::makeNative(EventKind::kNativeHide,
                                                   EventOrigin::kBaseUi, 3),
                          gameui::menu::makeNative(EventKind::kNativeHide,
                                                   EventOrigin::kGameUi, 3),
                      },
                  .phase = BackendPhase::kInGame,
              },
              {
                  // Re-delivered hide: dropped, still closed, no action.
                  .events = {gameui::menu::makeNative(EventKind::kNativeHide,
                                                      EventOrigin::kBaseUi, 3)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // HideGameUI observed in a no-map world is refused: Main must stay.
          "native-hide-refused-no-map",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeNative(EventKind::kNativeHide,
                                                      EventOrigin::kGameUi, 1)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Cancel retires the generation: the late result can never win.
          "loading-cancel-result-race",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeConnectRequest(
                                 "sv.example:27015"),
                             gameui::menu::makeLoadingStarted(1)},
                  .phase = BackendPhase::kLoading,
                  .pages = {MenuPage::kLoading},
                  .actions = {"connect:1:sv.example:27015"},
              },
              {
                  .events = {MenuEvent{EventKind::kConnectCancel}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .actions = {"cancelConnect:1"},
              },
              {
                  // The cancelled attempt's late success: dropped - no InGame,
                  // no phase change, no action.
                  .events = {gameui::menu::makeLoadingFinished(
                      1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Success first, cancel later: no attempt is live, nothing happens.
          "result-then-late-cancel",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeConnectRequest("a.example:1"),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
                  .actions = {"connect:1:a.example:1"},
              },
              {
                  .events = {MenuEvent{EventKind::kConnectCancel}},
                  .phase = BackendPhase::kInGame,
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
          },
      },
      {
          // Session-1 leftovers after session 2 started are dropped by
          // generation and session; the live attempt still completes.
          "stale-session-and-generation",
          {
              {
                  .events =
                      {
                          gameui::menu::makePhaseChanged(
                              1, BackendPhase::kDisconnected),
                          gameui::menu::makeConnectRequest("old.example:1"),
                          MenuEvent{EventKind::kConnectCancel},
                          gameui::menu::makePhaseChanged(
                              2, BackendPhase::kDisconnected),
                          gameui::menu::makeConnectRequest("new.example:1"),
                      },
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"connect:1:old.example:1", "cancelConnect:1",
                              "connect:2:new.example:1"},
              },
              {
                  .events = {gameui::menu::makeLoadingStarted(1)},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
              },
              {
                  .events = {gameui::menu::makeLoadingFinished(
                      1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
              },
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
              },
              {
                  .events = {gameui::menu::makeLoadingStarted(2),
                             gameui::menu::makeLoadingFinished(
                                 2, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // Shutdown latches: state wiped, no exclusive input, and every
          // later event - native, intent, backend - is dropped silently.
          "shutdown-terminal",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeOpenPage(MenuPage::kSettings),
                             MenuEvent{EventKind::kConsoleToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain, MenuPage::kSettings},
                  .overlays = {MenuOverlay::kConsole},
              },
              {
                  .events = {MenuEvent{EventKind::kShutdown}},
                  .phase = BackendPhase::kShuttingDown,
              },
              {
                  .events =
                      {
                          MenuEvent{EventKind::kMenuToggle},
                          gameui::menu::makeConnectRequest("x.example:1"),
                          gameui::menu::makePhaseChanged(2,
                                                         BackendPhase::kInGame),
                          gameui::menu::makeNative(EventKind::kNativeActivate,
                                                   EventOrigin::kBaseUi, 9),
                          MenuEvent{EventKind::kQuitRequested},
                          gameui::menu::makeLoadingFinished(
                              9, LoadingOutcome::kSuccess, nullptr),
                      },
                  .phase = BackendPhase::kShuttingDown,
              },
              {
                  .events = {MenuEvent{EventKind::kShutdown}},
                  .phase = BackendPhase::kShuttingDown,
              },
          },
      },
      {
          // Failure returns to Main with the reason as an owned copy; the
          // error overlay is one ESC away from the home.
          "connect-failure-error-overlay",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeConnectRequest("bad.example:1"),
                             gameui::menu::makeLoadingStarted(1),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kFailed,
                                 "#GameUI_ConnectionFailed")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConnectionError},
                  .actions = {"connect:1:bad.example:1"},
                  .errorReason = "#GameUI_ConnectionFailed",
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Password-required keeps the attempt alive; the typed submission
          // reaches the backend; the retry can then succeed into the game.
          "password-retry",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeConnectRequest("pw.example:1"),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kPasswordRequired,
                                 nullptr)},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .overlays = {MenuOverlay::kPassword},
                  .actions = {"connect:1:pw.example:1"},
              },
              {
                  .events = {gameui::menu::makePasswordSubmitted("hunter2")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .overlays = {MenuOverlay::kPassword},
                  .actions = {"submitPassword:1:hunter2"},
              },
              {
                  .events = {gameui::menu::makeLoadingFinished(
                      1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // T14 direct-connect overlay: pushed from the no-map home, ESC
          // pops exactly one layer, closed before a connect so the attempt
          // starts from a clean overlay stack; refused in-game.
          "directconnect-toggle",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kDirectConnectToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kDirectConnect},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kDirectConnectToggle},
                             MenuEvent{EventKind::kDirectConnectToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  // In-game: the address entry belongs to the no-map world.
                  .events = {gameui::menu::makePhaseChanged(
                                 2, BackendPhase::kInGame),
                             MenuEvent{EventKind::kDirectConnectToggle}},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // T14: connect from the direct-connect overlay clears the overlay
          // (the loading page owns the screen), and a late event of the
          // retired generation after a cancel lands nowhere.
          "directconnect-connect-clears-and-cancel-late-drop",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kDirectConnectToggle},
                             gameui::menu::makeConnectRequest(
                                 "new.example:27015")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"connect:1:new.example:27015"},
              },
              {
                  .events = {MenuEvent{EventKind::kConnectCancel}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .actions = {"cancelConnect:1"},
              },
              {
                  // Late engine events for the dead generation: dropped,
                  // state unchanged, no backend action.
                  .events = {gameui::menu::makeLoadingStarted(1),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kSuccess, nullptr),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kFailed, "late")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // T14: failure keeps the reason, retry issues a NEW generation,
          // and the second failure carries its own reason (the overlay
          // re-projects from the controller's owned copy).
          "failure-retry-new-generation",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeConnectRequest(
                                 "flaky.example:1"),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kFailed,
                                 "#GameUI_ConnectionFailed")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConnectionError},
                  .actions = {"connect:1:flaky.example:1"},
                  .errorReason = "#GameUI_ConnectionFailed",
              },
              {
                  .events = {gameui::menu::makeConnectRequest(
                      "flaky.example:1")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"connect:2:flaky.example:1"},
              },
              {
                  .events = {gameui::menu::makeLoadingFinished(
                      2, LoadingOutcome::kFailed, "Timed out")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConnectionError},
                  .errorReason = "Timed out",
              },
          },
      },
      {
          // Disconnect cleans pages, overlays and focus in one step; the
          // backend's own disconnected event re-affirms idempotently.
          "disconnect-cleans-pages-and-focus",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeConnectRequest("g.example:1"),
                             gameui::menu::makeLoadingStarted(1),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kSuccess, nullptr),
                             MenuEvent{EventKind::kMenuToggle},
                             gameui::menu::makeOpenPage(MenuPage::kSettings),
                             MenuEvent{EventKind::kConsoleToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause, MenuPage::kSettings},
                  .overlays = {MenuOverlay::kConsole},
                  .actions = {"connect:1:g.example:1"},
              },
              {
                  .events = {MenuEvent{EventKind::kDisconnectRequested}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .actions = {"disconnect"},
              },
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // Fatal: emergency page is not dismissable; quit stays reachable.
          "fatal-emergency-quit",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeFatal("renderer context lost")},
                  .phase = BackendPhase::kFatal,
                  .overlays = {MenuOverlay::kEmergency},
                  .errorReason = "renderer context lost",
              },
              {
                  .events = {MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kFatal,
                  .overlays = {MenuOverlay::kEmergency},
              },
              {
                  .events = {MenuEvent{EventKind::kQuitRequested}},
                  .phase = BackendPhase::kFatal,
                  .overlays = {MenuOverlay::kEmergency,
                               MenuOverlay::kConfirmQuit},
              },
              {
                  .events = {gameui::menu::makeQuitResolved(true)},
                  .phase = BackendPhase::kFatal,
                  .overlays = {MenuOverlay::kEmergency},
                  .actions = {"quit"},
              },
          },
      },
      {
          // Direct resume closes everything (console included); no-map
          // resume is refused.
          "resume-intent",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             gameui::menu::makeConnectRequest("r.example:1"),
                             gameui::menu::makeLoadingStarted(1),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kSuccess, nullptr),
                             MenuEvent{EventKind::kMenuToggle}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
                  .actions = {"connect:1:r.example:1"},
              },
              {
                  .events = {MenuEvent{EventKind::kConsoleToggle},
                             MenuEvent{EventKind::kResume}},
                  .phase = BackendPhase::kInGame,
                  .actions = {"resume"},
              },
              {
                  .events = {gameui::menu::makePhaseChanged(
                      1, BackendPhase::kDisconnected)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {MenuEvent{EventKind::kResume}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // T09: a confirmed quit fires the backend exactly once - a second
          // resolve lands on no confirm overlay, and ESC pops the overlay
          // without quitting (cancel must never reach the backend).
          "quit-confirm-once",
          {
              enterGame("q.example:1"),
              {
                  .events = {MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kQuitRequested}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
                  .overlays = {MenuOverlay::kConfirmQuit},
              },
              {
                  // Confirm: the overlay pops and the backend quits once...
                  .events = {gameui::menu::makeQuitResolved(true)},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
                  .actions = {"quit"},
              },
              {
                  // ...and a duplicated confirm has nothing to resolve.
                  .events = {gameui::menu::makeQuitResolved(true)},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
          },
      },
      {
          // T09: quit cancel (ESC on the overlay or the No button) pops the
          // overlay only - the backend never sees a quit.
          "quit-cancel",
          {
              enterGame("c.example:9"),
              {
                  .events = {MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kQuitRequested}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
                  .overlays = {MenuOverlay::kConfirmQuit},
              },
              {
                  .events = {MenuEvent{EventKind::kBack}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  // Re-request, then decline through the button path.
                  .events = {MenuEvent{EventKind::kQuitRequested},
                             gameui::menu::makeQuitResolved(false)},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  .events = {MenuEvent{EventKind::kDisconnectRequested}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .actions = {"disconnect"},
              },
              {
                  // Disconnect is one-shot per session: the phase already
                  // left InGame, a repeated intent does nothing.
                  .events = {MenuEvent{EventKind::kDisconnectRequested}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // T09: bridge-sync events. The suppression bridge coalesces the
          // dual-fired engine callbacks, so the sink fires once per
          // transition: kMenuShown opens idempotently, kMenuHidden
          // force-clears without any backend action.
          "bridge-sync",
          {
              enterGame("b.example:1"),
              {
                  // In-game show opens Pause; a repeated show changes nothing.
                  .events = {MenuEvent{EventKind::kMenuShown},
                             MenuEvent{EventKind::kMenuShown}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  // A bridge-initiated hide (focus loss) clears everything -
                  // including overlays - and never resumes.
                  .events = {MenuEvent{EventKind::kQuitRequested},
                             MenuEvent{EventKind::kMenuHidden}},
                  .phase = BackendPhase::kInGame,
              },
              {
                  // The menu can come straight back up.
                  .events = {MenuEvent{EventKind::kMenuShown}},
                  .phase = BackendPhase::kInGame,
                  .pages = {MenuPage::kPause},
              },
              {
                  .events = {MenuEvent{EventKind::kMenuHidden}},
                  .phase = BackendPhase::kInGame,
              },
              {
                  // No-map show opens Main, also idempotent.
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kMenuShown},
                             MenuEvent{EventKind::kMenuShown}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  // A decided close leaves nothing for the sync to clear.
                  .events = {MenuEvent{EventKind::kMenuToggle},
                             MenuEvent{EventKind::kMenuHidden}},
                  .phase = BackendPhase::kDisconnected,
              },
          },
      },
      {
          // T19: the local-game attempt is the same machinery as a connect -
          // one generation, the Loading page, one cancel path - with the map
          // token as the payload and startLocalGame as the backend action.
          "create-game-attempt",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kMenuShown}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  // Empty map never starts an attempt.
                  .events = {gameui::menu::makeCreateGameRequest("")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
              {
                  .events = {gameui::menu::makeCreateGameRequest("osprey")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"startLocalGame:1:osprey"},
              },
              {
                  // The engine's LoadingStarted moves to Loading.
                  .events = {gameui::menu::makeLoadingStarted(1)},
                  .phase = BackendPhase::kLoading,
                  .pages = {MenuPage::kLoading},
              },
              {
                  // Cancel retires the local attempt exactly like a connect.
                  .events = {MenuEvent{EventKind::kConnectCancel}},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .actions = {"cancelConnect:1"},
              },
              {
                  // A late success of the retired generation is dropped.
                  .events = {gameui::menu::makeLoadingFinished(
                      1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
              },
          },
      },
      {
          // T19: a refused launch fails through the SAME typed path as a
          // connect failure (generation-tagged kLoadingFinished); the
          // attempt is over, the error overlay is up, and a retry is a NEW
          // generation with the same payload.
          "create-game-refused-then-retry",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kMenuShown},
                             gameui::menu::makeCreateGameRequest("osprey")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"startLocalGame:1:osprey"},
              },
              {
                  .events = {gameui::menu::makeLoadingFinished(
                      1, LoadingOutcome::kFailed, "#CG_ErrorWrite")},
                  .phase = BackendPhase::kDisconnected,
                  .pages = {MenuPage::kMain},
                  .overlays = {MenuOverlay::kConnectionError},
                  .errorReason = "#CG_ErrorWrite",
              },
              {
                  // Retry from the overlay: a fresh generation, same map.
                  .events = {gameui::menu::makeCreateGameRequest("osprey")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"startLocalGame:2:osprey"},
              },
              {
                  // The join beat closes the menu like any successful attempt.
                  .events = {gameui::menu::makeLoadingStarted(2),
                             gameui::menu::makeLoadingFinished(
                                 2, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // An engine changelevel arrives with no connect attempt behind it.
          // It must allocate its own generation and put the Loading page up
          // (instead of leaving InGame, where the next ActivateGameUI would
          // reconcile the cleared level fact into the no-map Main page).
          "map-change-loading",
          {
              enterGame("127.0.0.1:27015"),
              {
                  .events = {gameui::menu::makeMapLoadStarted()},
                  .phase = BackendPhase::kLoading,
                  .pages = {MenuPage::kLoading},
              },
              {
                  // The engine stream continues on the allocated generation.
                  .events = {gameui::menu::makeLoadingStarted(2)},
                  .phase = BackendPhase::kLoading,
                  .pages = {MenuPage::kLoading},
              },
              {
                  // LoadingFinished("level") ends it: back in game, menu
                  // closed, generation retired.
                  .events = {gameui::menu::makeLoadingFinished(
                      2, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
      {
          // A live connect attempt owns the loading stream; a map-load hint
          // must not steal its generation or replace its page.
          "map-change-during-connect-ignored",
          {
              {
                  .events = {gameui::menu::makePhaseChanged(
                                 1, BackendPhase::kDisconnected),
                             MenuEvent{EventKind::kMenuShown},
                             gameui::menu::makeConnectRequest(
                                 "127.0.0.1:27015")},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
                  .actions = {"connect:1:127.0.0.1:27015"},
              },
              {
                  .events = {gameui::menu::makeMapLoadStarted()},
                  .phase = BackendPhase::kConnecting,
                  .pages = {MenuPage::kLoading},
              },
              {
                  .events = {gameui::menu::makeLoadingStarted(1),
                             gameui::menu::makeLoadingFinished(
                                 1, LoadingOutcome::kSuccess, nullptr)},
                  .phase = BackendPhase::kInGame,
              },
          },
      },
  };

  for (const Case &testCase : cases)
    runCase(testCase);
  runStandaloneChecks();

  return abctest::finish("abc_ui_menu_state");
}
