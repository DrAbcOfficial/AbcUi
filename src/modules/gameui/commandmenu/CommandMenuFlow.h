#pragma once

// gameui::CommandMenuFlow - the in-game command menu's open/close intents
// and claimed-key feed.
//
// The page became a module (abcui.gameui, sdk commandmenu section), so the
// flow keeps what is framework-side: the model, the definition hand-off,
// the request latch and the raised-menu discipline around open/close, and
// the key-edge feed (the +commandmenu close rule, then the
// menu.commandkey veto channel the page answers). Command execution stays
// here too: the services::commandmenu access point hands the page's
// activations to handleCommand (close first, then the engine send).

#include "../navigation/PageHost.h"
#include "../../../services/commandmenu/CommandMenuModel.h"

#include <functional>
#include <string>

namespace gameui {

// The command page is on top of the menu stack (the module document is the
// presented surface); the close rule is the old
// GameUiModule::closeCommandMenuPage recovery.
using CommandMenuHost = PageHost;

class CommandMenuFlow {
public:
  explicit CommandMenuFlow(CommandMenuHost host);

  void setDefinition(services::commandmenu::Definition definition);
  void shutdown();
  bool consumeRequest();
  void cancelPending();
  void resetForWorld();

  void toggle();
  void open();
  void close();
  void handleCommand(const std::string &command);
  bool handleKey(int keynum);

  services::commandmenu::Model &model() {
    return Model;
  }

private:
  CommandMenuHost Host;
  services::commandmenu::Model Model;
  bool Requested = false;
  bool RaisedMenu = false;
};

} // namespace gameui
