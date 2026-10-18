// Extracted from GameUiModule.cpp (UTF-8). Responsibility:
// playerlist/PlayerListFlow.
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include <cstdio>
#include "modules/gameui/navigation/MenuAction.h"
namespace gameui {
using namespace navigation;
void GameUiModule::presentPlayerListPage() {
  if (!PlayerListPage_)
    return;
  PlayerList.refresh();
  PlayerListPage_->render();
  PlayerListPage_->show();
  PlayerListPresented = true;
}

void GameUiModule::selectPlayerRow(
    const services::playerlist::SelectionKey &Key) {
  PlayerList.refresh();
  PlayerList.select(Key);
  if (PlayerListPage_)
    PlayerListPage_->render();
}

void GameUiModule::togglePlayerMute() {
  bool mutedAfter = false;
  const auto result = PlayerList.toggleMuted(mutedAfter);
  switch (result) {
  case services::playerlist::ToggleError::kNone:
    abc::ConsolePrint(
        "[AbcUi] player mute %s: idx=%d now=%d\n",
        mutedAfter ? "MUTED" : "UNMUTED",
        PlayerList.selectedEntry() ? PlayerList.selectedEntry()->index : 0,
        mutedAfter ? 1 : 0);
    break;
  case services::playerlist::ToggleError::kUnavailable:
    abc::ConsoleWarn(
        "[AbcUi] player mute unavailable on this install (no %s)\n",
        "GameClientExports001");
    break;
  case services::playerlist::ToggleError::kNoSelection:
    abc::ConsoleWarn("[AbcUi] player mute refused (no selection)\n");
    break;
  case services::playerlist::ToggleError::kIdentityChanged:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (selected player left or is a bot)\n");
    break;
  case services::playerlist::ToggleError::kReadbackFailed:
    abc::ConsoleWarn(
        "[AbcUi] player mute refused (backend did not change state)\n");
    break;
  case services::playerlist::ToggleError::kLocalPlayer:
    abc::ConsoleWarn("[AbcUi] player mute refused (local player)\n");
    break;
  }
  if (PlayerListPage_)
    PlayerListPage_->render();
}

void GameUiModule::handlePlayerListAction(ActionKind Kind) {
  // T20 page-internal intents: a row selection carries the identity
  // payload; the mute toggle and close are bare intents. Nothing
  // here reaches the engine except through the model's revalidated
  // backend call.
  if (Kind == ActionKind::kPlMute)
    togglePlayerMute();
  else if (Kind == ActionKind::kPlClose)
    Controller.dispatch(MenuEvent{EventKind::kBack});
  else {
    abc::ConsoleWarn("[AbcUi] unknown player-list action \"%s\" dropped\n",
                     actionName(Kind));
    return;
  }
  finishDispatch();
  return;
}

} // namespace gameui
