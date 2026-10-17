// In-game command menu flow: +commandmenu ownership, page lifecycle and the
// one engine command boundary.
#include "modules/gameui/GameUiModule.h"

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include "modules/gameui/engine/MenuBindings.h"
#include "rmlui/KeyMap.h"
#include "rmlui/VGUI2ExtensionImport.h"

#include <cstring>
#include <string>

namespace gameui {

void GameUiModule::toggleCommandMenuPage() {
	// A second press that arrives before the Rml context exists cancels the
	// pending open instead of replaying a stale intent later.
	if (CommandMenuRequested) {
		CommandMenuRequested = false;
		return;
	}
	if (CommandMenuPage_ && CommandMenuPage_->isVisible()) {
		closeCommandMenuPage();
		return;
	}
	openCommandMenuPage();
}

void GameUiModule::openCommandMenuPage() {
	if (!Shell || !Context || !CommandMenuPage_) {
		// HUD_Init can observe the command before the first HUD_Frame builds
		// the Rml context. Replay the intent once context assembly finishes.
		CommandMenuRequested = true;
		return;
	}
	if (!services::nativeUiBridge().hasLevel()) {
		abc::ConsoleWarn("[AbcUi] commandmenu is only available while connected to a server\n");
		return;
	}
	if (!CommandMenu.hasDefinition()) {
		abc::ConsoleWarn("[AbcUi] commandmenu.json is unavailable; command menu not opened\n");
		return;
	}
	if (CommandMenuPage_->isDegraded()) {
		abc::ConsoleWarn("[AbcUi] commandmenu page is degraded; command menu not opened\n");
		return;
	}

	const bool menuWasUp = MenuPresented;
	if (!menuWasUp) {
		if (!services::nativeUiBridge().menuUp())
			services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
		if (!MenuPresented)
			presentMenu();
	}
	if (!MenuPresented)
		return;
	CommandMenuRaisedMenu = !menuWasUp;

	reconcilePhase();
	if (Controller.state().phase != menu::BackendPhase::kInGame) {
		if (CommandMenuRaisedMenu)
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
		CommandMenuRaisedMenu = false;
		return;
	}

	CommandMenuPage_->prepare(engine::currentMapName());
	Controller.dispatch(menu::makeOpenPage(MenuPage::kCommandMenu));
	CommandMenuRequested = false;
	finishDispatch();
}

void GameUiModule::presentCommandMenuPage() {
	if (!CommandMenuPage_)
		return;
	const bool wasVisible = CommandMenuPage_->isVisible();
	CommandMenuPage_->render();
	CommandMenuPage_->show();
	if (!wasVisible)
		CommandMenuPage_->refocusDefault();
	CommandMenuPresented = true;
}

void GameUiModule::closeCommandMenuPage() {
	CommandMenuRequested = false;
	const bool raisedMenu = CommandMenuRaisedMenu;
	CommandMenuRaisedMenu = false;
	if (CommandMenuPage_) {
		CommandMenuPage_->hide();
		CommandMenuPresented = false;
	}
	if (Controller.state().pages.topIs(MenuPage::kCommandMenu)) {
		Controller.dispatch(MenuEvent{EventKind::kBack}); // command page -> Pause
		if (raisedMenu) {
			if (Controller.state().pages.topIs(MenuPage::kPause))
				Controller.dispatch(MenuEvent{EventKind::kResume}); // close + resume
			// A phase change can make the decided resume illegal. The menu
			// command still owns this presentation, so force-clear the host
			// instead of leaving the pause page behind it.
			if (Controller.isMenuActive())
				Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
		}
	}
	finishDispatch();
}

void GameUiModule::handleCommandMenuCommand(const std::string& command) {
	if (command.empty())
		return;
	// Close first: the shared engine command path may synchronously raise a
	// replacement page (votemenu, console commands, media player, ...).
	closeCommandMenuPage();
	std::string line = command;
	line.push_back('\n');
	abc::EngineClientCmd(line.c_str());
}

bool GameUiModule::handleCommandMenuKey(int keynum) {
	if (!CommandMenuPage_ || !CommandMenuPage_->isVisible())
		return false;

	// While the Rml page owns input the engine never dispatches the +command
	// binding; close on the same physical key by reading the live binding.
	const char* binding = vgui2ext::keyBindingForKey(keynum);
	if (binding && _stricmp(binding, "+commandmenu") == 0) {
		closeCommandMenuPage();
		return true;
	}

	const Rml::Input::KeyIdentifier key = rmlui::mapKeyCode(keynum);
	if (key == Rml::Input::KI_UNKNOWN)
		return false;
	return CommandMenuPage_->handleKey(key);
}

} // namespace gameui
