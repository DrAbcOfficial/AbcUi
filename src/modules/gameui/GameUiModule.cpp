// gameui feature module - assembly point of the menu replacement domain.
// T03 shipped the empty seam; T05 hangs the minimal interactive page's
// lifetime off the module contract (modules/Module.h: documents die with
// the context, so the page is built in onContextReady and released in
// onShutdown while the context still exists).
//
// The Debug-only abc_ui_menu_page command is the "not formally taken over"
// diagnostic mode of the T05 acceptance: it toggles the page over the
// existing menus and reuses the existing UI-mouse owner flag (the
// scoreboard pointer-mode seam) so the page is mouse-reachable. It is NOT
// input ownership: keyboard arbitration lands in T06, typed characters in
// T07, and the original GameUI stays untouched (suppression is T08).
#include "../Module.h"

#include <RmlUi/Core/Context.h>

#include <abc/Console.h>

#include "../../panels/mainmenu/MainMenuPage.h"

#ifndef NDEBUG
#include <abc/CvarCmd.h>
#include "../../rmlui/RmlUiModule.h"
#endif

namespace gameui {

namespace {

class GameUiModule final : public modules::IUiModule {
public:
	GameUiModule() {
		modules::registry().add(this);
		Instance = this;
	}

	const char* name() const override { return "gameui"; }

	void onInit() override {
#ifndef NDEBUG
		// HUD_Init guarded block only (plan 08 §4.9 red line).
		abc::ADD_COMMAND("abc_ui_menu_page", menuPageCommand);
#endif
	}

	void onContextReady(Rml::Context* context) override {
		Context = context;
		Page = std::make_unique<panels::mainmenu::MainMenuPage>();
		// T05 action seam: recording fake only - no real backend action can
		// fire from the page until T09 binds the MenuController.
		Page->setActionSink([](const char* action) {
			abc::ConsolePrint("[AbcUi] menu page action \"%s\" (fake backend)\n", action);
		});
		using LoadOutcome = framework::InteractiveDocument::LoadOutcome;
		switch (Page->load(context)) {
			case LoadOutcome::kLoaded:
				abc::ConsolePrint("[AbcUi] menu page loaded (hidden; %s toggles it in debug builds)\n",
					"abc_ui_menu_page");
				break;
			case LoadOutcome::kEmergencyFallback:
				abc::ConsoleWarn("[AbcUi] menu page degraded to the emergency document\n");
				break;
			case LoadOutcome::kFailed:
				abc::ConsoleWarn("[AbcUi] menu page failed to load entirely (T21 strict path)\n");
				break;
		}
	}

	void onShutdown() override {
		// Context still alive here: the page closes its document and drops
		// every listener before the DOM dies (module contract).
		Page.reset();
		Context = nullptr;
	}

private:
#ifndef NDEBUG
	static void menuPageCommand() {
		if (!Instance || !Instance->Page || !Instance->Context) {
			abc::ConsoleWarn("[AbcUi] menu page not available\n");
			return;
		}
		if (Instance->Page->isVisible()) {
			Instance->Page->hide();
			rmlui::setUiMouseOwner(false);
			abc::ConsolePrint("[AbcUi] menu page hidden\n");
			return;
		}
		Instance->Page->show();
		// Diagnostic reachability, not input ownership (see header note):
		// the page must be mouse-hit-testable over the existing menus before
		// T06 lands the real ownership stack.
		rmlui::setUiMouseOwner(true);
		if (Rml::Element* focus = Instance->Context->GetFocusElement())
			abc::ConsolePrint("[AbcUi] menu page shown, focus on '%s' (doc '%s')\n",
				focus->GetId().c_str(),
				focus->GetOwnerDocument() ? focus->GetOwnerDocument()->GetId().c_str() : "<none>");
		else
			abc::ConsoleWarn("[AbcUi] menu page shown, no focus element\n");
	}
#endif

	static GameUiModule* Instance;

	Rml::Context* Context = nullptr;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Page;
};

GameUiModule* GameUiModule::Instance = nullptr;

// The static object of this TU is the module's owner and destructor; the
// registry only borrows the pointer (coding rule 11).
const GameUiModule RegisterModule;

}

}
