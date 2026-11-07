#pragma once

// The host callbacks a page flow needs from the composition root: the
// shell/phase boundary around one menu page. CommandMenuHost and
// InventoryHost are aliases; the page id is the only difference in their
// construction (GameUiModule::MakePageHost).

#include <functional>

namespace gameui {

struct PageHost {
  std::function<bool()> shellReady;
  // The page is on top of the menu stack.
  std::function<bool()> pageOnTop;
  // Raises the menu when needed and reports whether this call did so.
  std::function<bool(bool &raisedMenu)> ensureMenu;
  std::function<void()> cancelRaisedMenu;
  std::function<bool()> reconcileInGame;
  std::function<void()> dispatchOpen;
  // Closes the page's navigation state; `raisedMenu` applies the paused-menu
  // recovery.
  std::function<void(bool raisedMenu)> closeNavigation;
  std::function<void()> finishDispatch;
};

// Raises the menu (recording whether this call did so) and reconciles the
// backend phase; restores the raised menu before reporting failure. The
// caller dispatches its page open only when this returns true.
inline bool ensureMenuAndPhase(PageHost &host, bool &raisedMenu) {
  raisedMenu = false;
  if (!host.ensureMenu(raisedMenu))
    return false;
  if (host.reconcileInGame())
    return true;
  if (raisedMenu)
    host.cancelRaisedMenu();
  raisedMenu = false;
  return false;
}

} // namespace gameui
