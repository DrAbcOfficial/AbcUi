// Extracted from GameUiModule.cpp (UTF-8). Responsibility: navigation/Actions.
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include "MenuAction.h"
namespace gameui {
using namespace navigation;
void GameUiModule::onMenuAction(const char* action) {
	const Action Parsed = parseAction(action ? action : "");
	const ActionKind Kind = Parsed.Kind;
	if (Kind == ActionKind::kResume)
		Controller.dispatch(MenuEvent{EventKind::kResume});
	else if (Kind == ActionKind::kDisconnect)
		Controller.dispatch(MenuEvent{EventKind::kDisconnectRequested});
	else if (Kind == ActionKind::kQuit)
		Controller.dispatch(MenuEvent{EventKind::kQuitRequested});
	else if (Kind == ActionKind::kQuitYes)
		Controller.dispatch(gameui::menu::makeQuitResolved(true));
	else if (Kind == ActionKind::kQuitNo)
		Controller.dispatch(gameui::menu::makeQuitResolved(false));
	else if (Kind == ActionKind::kSettings)
		Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kSettings));
	else if (Kind == ActionKind::kServers)
		Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kServers));
	else if (Kind == ActionKind::kCreateGame)
		Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kCreateGame));
	else if (Parsed.Domain == ActionDomain::kCreateGame) {
		handleCreateGameAction(Kind);
		return;
	}
	else if (Kind == ActionKind::kPlayerList)
		Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kPlayerList));
	else if (Parsed.Domain == ActionDomain::kPlayerList) {
		handlePlayerListAction(Kind);
		return;
	}
	else if (Kind == ActionKind::kConsole || Kind == ActionKind::kConsoleClose) {
		// T16: the main-menu console entry toggles the replacement
		// console through the bridge (the same funnel as the engine
		// entries), so the engine-facing IsConsoleVisible answer and the
		// overlay stack never disagree.
		auto& bridge = services::nativeUiBridge();
		// Close is an idempotent intent, not a toggle: a repeated click or
		// delayed close after an engine Hide must never reopen the console.
		if (Kind == ActionKind::kConsoleClose || bridge.consoleRequested())
			bridge.requestConsoleClose(services::NativeUiOrigin::kOwnUi);
		else
			bridge.requestConsoleOpen(services::NativeUiOrigin::kOwnUi);
		finishDispatch();
		return;
	}
	else if (Parsed.Domain == ActionDomain::kConnection) {
		handleConnectionAction(Kind);
		return;
	}
	else if (Kind == ActionKind::kReturn || Kind == ActionKind::kClose)
		Controller.dispatch(MenuEvent{EventKind::kBack});
	else if (Kind == ActionKind::kBindCaptureBegin) {
		// T11: the settings page started a capture session in the bind
		// model; the module raises the kCapture ownership tier above the
		// menu page so the next physical edge is captured, not played.
		startKeyCapture();
		return; // no controller transition - the page state already moved
	}
	else if (Parsed.Domain == ActionDomain::kServers) {
		handleServerAction(Kind);
		return;
	}
	else if (Parsed.Domain == ActionDomain::kVideo) {
		handleVideoAction(Kind);
		return;
	}
	else {
		abc::ConsoleWarn("[AbcUi] unknown menu action \"%s\" dropped\n", action ? action : "");
		return;
	}
	finishDispatch();
}


void GameUiModule::finishDispatch() {
	syncAttemptState();
	// T19: a refused local launch (cvar write failed / seams unwired)
	// fails the attempt through the SAME typed path a connect failure
	// uses - the error overlay with the reason token. The controller
	// already started the attempt (generation + Loading page), so this
	// must run after the model sync above made the generation live.
	if (consumeLaunchFailure())
		syncAttemptState();  // the refused attempt is over: retire + re-project
	if (MenuPresented)
		syncMenuPresentation();
	if (!Controller.isMenuActive() && services::nativeUiBridge().menuUp())
		services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
}

}
