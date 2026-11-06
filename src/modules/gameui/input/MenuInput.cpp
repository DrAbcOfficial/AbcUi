// Extracted from GameUiModule.cpp (UTF-8). Responsibility: input/MenuInput.
#include "modules/gameui/GameUiModule.h"
#include "modsys/ApiTable.h"
#include <abc/Console.h>
#include <keydefs.h>
#include "rmlui/InputBridge.h"
#include "rmlui/TextInput.h"
#include <string>
namespace gameui {
void GameUiModule::startKeyCapture() {
  // The bind model's session and the key-code row session share one token
  // and one clamp; either one being live starts it.
  if (CaptureToken.held())
    return;
  if (!BindModel.capturing() && KeyCaptureCvar.empty())
    return;
#ifndef NDEBUG
  abc::ConsolePrint("[AbcUi] capture start row=%d cvar=%s\n",
                    BindModel.captureRow(), KeyCaptureCvar.c_str());
#endif
  rmlui::setKeyCaptureClamp(true);
  CaptureToken = rmlui::uiRouter().acquire(
      &CaptureOwner, framework::uiinput::OwnerTier::kCapture);
  if (!CaptureToken.held()) {
    // Unrepresentable depth (8 owners): fail closed - no session.
    BindModel.cancelCapture();
    KeyCaptureCvar.clear();
    rmlui::setKeyCaptureClamp(false);
    abc::ConsoleWarn("[AbcUi] capture token unavailable (router full)\n");
  }
}

// One key-code row (the vote answer keys): the row's draft receives the
// captured key number, so the write still waits for the page's Apply.
bool GameUiModule::beginRowKeyCapture(const std::string &cvar) {
  if (cvar.empty() || CaptureToken.held() || !KeyCaptureCvar.empty())
    return false;
  // One session at a time: the bind capture owns the model and the token.
  if (BindModel.capturing())
    return false;
  const auto view = SettingsModel.find(cvar.c_str());
  if (!view || !view->row || !view->row->KeyCodeRow || view->missing ||
      !SettingsModel.editable(cvar.c_str()))
    return false;
  KeyCaptureCvar = cvar;
  startKeyCapture();
  if (!CaptureToken.held()) {
    KeyCaptureCvar.clear(); // startKeyCapture already warned and clamped off
    return false;
  }
  return true;
}

void GameUiModule::cancelRowKeyCapture() {
  if (KeyCaptureCvar.empty())
    return;
  KeyCaptureCvar.clear();
  // The bind model owns the shared token when its own session is live.
  if (!BindModel.capturing())
    endKeyCapture();
}

void GameUiModule::endKeyCapture() {
  CaptureToken.release();
  rmlui::setKeyCaptureClamp(false);
}

void GameUiModule::onCapturedKeyEdge(int keynum) {
  if (!BindModel.capturing()) {
    // The key-code row session: the edge becomes the row's draft value. ESC
    // closes the session without touching the draft, exactly like a
    // cancelled bind capture; the page then re-projects from the published
    // generation.
    const std::string cvar = KeyCaptureCvar;
    KeyCaptureCvar.clear();
    // ESC cancels; a non-key code (negative/zero) is never persisted as a
    // binding.
    const bool rejected = keynum <= 0 || keynum == K_ESCAPE;
    if (!cvar.empty() && !rejected)
      SettingsModel.setDraftText(cvar.c_str(), std::to_string(keynum).c_str());
    endKeyCapture(); // exactly one edge per session, whatever it was
#ifndef NDEBUG
    abc::ConsolePrint("[AbcUi] key capture cvar=%s key=%d accepted=%d\n",
                      cvar.c_str(), keynum, !rejected);
#endif
    framework::SettingsApi::bumpRevision();
    return;
  }
  // Only the Debug trace reads the result.
  [[maybe_unused]] const auto result = BindModel.captureKey(keynum);
#ifndef NDEBUG
  abc::ConsolePrint("[AbcUi] capture key=%d result=%d conflict=%d\n", keynum,
                    static_cast<int>(result),
                    BindModel.conflictPending() ? 1 : 0);
#endif
  endKeyCapture(); // exactly one edge per session, whatever it was
  // The page re-projects from the published generation (the bind rows moved).
  framework::SettingsApi::bumpRevision();
}

void GameUiModule::onKeyCaptureForceLost() {
  BindModel.cancelCapture();
  KeyCaptureCvar.clear();
  rmlui::setKeyCaptureClamp(false);
}

bool GameUiModule::claimsKey([[maybe_unused]] int down,
                             [[maybe_unused]] int keynum) {
  return true;
}

bool GameUiModule::claimsWheel() {
  return true;
}

void GameUiModule::onClaimedKeyDown(int keynum) {
  // Exactly once per physical press (the router suppresses auto-repeat
  // here); the release pairs the just-fed ESC down inside the router.
  if (Controller.state().overlays.empty() && Command->handleKey(keynum))
    return;
  // Issue 10: the inventory window owns ESC/arrows/Enter/Delete while it is
  // up (its Esc closes the window and returns to gameplay, never the pause
  // page). Placed after the command menu: only the top page is visible, so
  // at most one of the two claims the key.
  if (Controller.state().overlays.empty() && Inventory->handleKey(keynum))
    return;
  if (keynum == '`' || keynum == '~') {
    // T16 console hotkey (the ` / toggleconsole key): while the menu
    // owns the keyboard the engine never sees this edge, so the
    // overlay toggle lives here. The key's own committed character
    // ("`"/"~" through the T07 text pipeline) is swallowed - the
    // toggle key never types (vanilla parity).
    if (!MenuPresented)
      return;
    rmlui::swallowToggleKeyText();
    // In-game hotkey close: when the console sits alone over the pause
    // page, one press closes the console AND returns to gameplay. On the
    // no-map home the menu must stay; deeper pages/modals keep theirs.
    const bool closeMenu = Controller.state().consoleHotkeyClosesMenu();
    onMenuAction("console");
    if (closeMenu) {
      Controller.dispatch(MenuEvent{EventKind::kResume});
      finishDispatch();
    }
    return;
  }
  if (keynum != K_ESCAPE || !MenuPresented)
    return;
  // T16: an ESC press the console page's own layers (context menu /
  // confirm pane / completion list) consumed (the Rml feed runs before
  // this action) closes only that layer - "同一事件不关闭两层". The claim
  // rides the sdk consolepage section.
  if (framework::ConsolePageApi::consumeEscapeClaim())
    return;
  // T07.b ESC priority: the first press cancels a running composition
  // instead of navigating; the next press toggles. The IME normally
  // swallows ESC itself, so this is the leak path.
  if (rmlui::textComposing()) {
    rmlui::cancelComposition();
    return;
  }
  // T12: an outstanding display confirmation is modal - ESC is consumed
  // (the dialog resolves by button or by its own deadline; a dismissal
  // cannot silently abandon an unconfirmed mode).
  if (VideoModel.awaitingConfirm())
    return;
  // The state machine owns the ESC semantics: pop one overlay, return
  // from a sub page, or close+resume on the pause home (T04 table).
  // The phase reconciles AFTER the toggle: a menu that survived a map
  // load (stale no-map phase) closes on the phase arrival instead of
  // re-opening as Pause from the cleared stack.
  // T10 exception: ESC on a dirty settings page is consumed by the
  // unsaved-changes prompt (second press dismisses it = stay) - the
  // controller's plain pop would silently discard the drafts.
  if (Controller.state().overlays.empty() &&
      Controller.state().pages.topIs(MenuPage::kServers) &&
      framework::ServerBrowserApi::consumeEscapeClaim())
    return;
  if (Controller.state().overlays.empty() &&
      Controller.state().pages.topIs(MenuPage::kSettings) &&
      framework::SettingsApi::consumeEscape()) {
    // The page owns the answer: a spray picker or the favorite menu in
    // flight closes on ESC, a dirty page raises the unsaved-changes prompt,
    // and a clean page reports "not consumed" so the menu pops it below.
    syncMenuPresentation();
    return;
  }
  Controller.dispatch(MenuEvent{EventKind::kMenuToggle});
  reconcilePhase();
  syncMenuPresentation();
  if (!Controller.isMenuActive())
    services::nativeUiBridge().requestMenuDown(
        services::NativeUiOrigin::kOwnUi);
}

void GameUiModule::onOwnershipLost() {
  // Force-release (focus loss / teardown): the bridge state must follow
  // so a later ESC cannot route into an empty router. The dismissal
  // clears the controller and the documents in one place.
  services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
}

} // namespace gameui
