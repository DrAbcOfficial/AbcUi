#pragma once

// gameui::InventoryFlow - the in-game inventory window's open/close intents
// and claimed-key feed.
//
// The page became a module (abcui.gameui, sdk inventory section), so the
// flow keeps what is framework-side: the model (fed by the InvAdd/InvRemove
// stream), the request latch and the raised-menu discipline around
// open/close, and the key-edge feed (the inventory/+inventory binding close
// rule, then the menu.commandkey veto channel the page answers). Actions
// run through the services::inventory access point (the window closes,
// then the client command runs).
//
// The chained inventory/+inventory/-inventory commands are the only
// open/close entry points besides the page's own close button; the native
// CInventoryPanel handler is swallowed by returning false from every hook.
//
// Actions are read-through: the model builds the command from the current
// selection, it is sent once, and no list mutation happens - a successful
// drop only removes the row when the server's InvRemove arrives.

#include "../navigation/PageHost.h"
#include "../../../services/inventory/InventoryModel.h"

#include <functional>

namespace gameui {

// The inventory page is on top of the menu stack (the module document is
// the presented surface); the close rule is CommandMenuFlow's recovery.
using InventoryHost = PageHost;

class InventoryFlow {
public:
  explicit InventoryFlow(InventoryHost host);

  void shutdown();
  bool consumeRequest();
  void resetForWorld();

  void open();
  void close();
  bool handleKey(int keynum);

  services::inventory::InventoryModel &model() {
    return Model;
  }

private:
  InventoryHost Host;
  services::inventory::InventoryModel Model;
  bool Requested = false;
  bool RaisedMenu = false;
};

} // namespace gameui
