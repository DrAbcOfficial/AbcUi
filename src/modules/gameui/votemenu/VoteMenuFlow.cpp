// Vote creation menu flow: command entry, navigation projection and submit.
#include "modules/gameui/GameUiModule.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <string>

namespace gameui {

void GameUiModule::onVidInit() {
	// A map transition invalidates the map catalog. The server sends a fresh
	// MapList for the next session; the page is not kept over a world change.
	VoteMenu.clearMapList();
	VoteMenuPresented = false;
	VoteMenuRequested = false;
	if (VoteMenuPage_)
		VoteMenuPage_->hide();
	// The command menu is also world-scoped: a map change closes it and
	// clears the one-shot request latch before the next session.
	CommandMenuRequested = false;
	CommandMenuRaisedMenu = false;
	if (CommandMenuPage_)
		CommandMenuPage_->hide();
	CommandMenuPresented = false;
}

void GameUiModule::openVoteMenuPage() {
	if (!Shell || !Context) {
		// The command can arrive in the short window between HUD_Init and Rml
		// assembly. Replay the intent once the context is ready.
		VoteMenuRequested = true;
		return;
	}
	if (!services::nativeUiBridge().hasLevel()) {
		abc::ConsoleWarn("[AbcUi] votemenu is only available while connected to a server\n");
		return;
	}
	if (!MenuPresented) {
		if (!services::nativeUiBridge().menuUp())
			services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
		if (!MenuPresented)
			presentMenu();
	}
	if (!MenuPresented)
		return;

	reconcilePhase();
	if (Controller.state().phase != menu::BackendPhase::kInGame)
		return;

	const bool freshOpening = !Controller.state().pages.topIs(MenuPage::kVoteMenu);
	if (freshOpening && VoteMenuPage_) {
		// The original dialog reopened on its Ban page. Keep the server map
		// catalogue, but reset tabs/filter/selection for this opening.
		VoteMenuPage_->prepare();
	}
	Controller.dispatch(menu::makeOpenPage(MenuPage::kVoteMenu));
	finishDispatch();
}

void GameUiModule::presentVoteMenuPage() {
	if (!VoteMenuPage_)
		return;
	VoteMenu.refresh();
	VoteMenuPage_->render();
	VoteMenuPage_->show();
	VoteMenuPresented = true;
}

void GameUiModule::handleVoteMenuAction(navigation::ActionKind kind) {
	using navigation::ActionKind;
	if (kind == ActionKind::kVoteMenuCancel) {
		Controller.dispatch(MenuEvent{EventKind::kBack});
		finishDispatch();
		return;
	}
	if (kind != ActionKind::kVoteMenuSubmit) {
		abc::ConsoleWarn("[AbcUi] unknown vote-menu action \"%s\" dropped\n",
			navigation::actionName(kind));
		return;
	}

	std::string command;
	const char* errorToken = nullptr;
	if (!VoteMenu.submitCommand(command, errorToken)) {
		VoteMenuPage_->render();
		VoteMenuPage_->showHint(errorToken);
		return;
	}
	// ABCEnchance sent these verbs through pfnServerCmd; keep that wire path.
	abc::ServerCmd(command.c_str());
	Controller.dispatch(MenuEvent{EventKind::kBack});
	finishDispatch();
}

} // namespace gameui
