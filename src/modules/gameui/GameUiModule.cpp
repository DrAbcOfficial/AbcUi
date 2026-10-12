// gameui feature module - assembly point of the menu replacement domain.
// T03 shipped the empty seam; T05 hangs the minimal interactive page's
// lifetime off the module contract (modules/Module.h: documents die with
// the context, so the page is built in onContextReady and released in
// onShutdown while the context still exists).
//
// T06: the diagnostic page toggle rides the real input-ownership stack -
// the module is a framework::uiinput::Owner at the menu tier. While the
// page owns input it claims every key edge (whatever the page does not use
// must still not leak into bindings) and the wheel; ESC runs the close
// action exactly once per physical press (the router fires it on the down
// transition only); keyup pairing, pre-held passthrough and focus-loss
// cleanup are the router's. This is per-owner routing, NOT the T08
// takeover (the original GameUI stays untouched) and not text input (T07).
#include "../Module.h"

#include <RmlUi/Core/Context.h>

#include <abc/Console.h>

#include <keydefs.h>

#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"

#ifndef NDEBUG
#include <abc/CvarCmd.h>
#endif

namespace gameui {

namespace {

class GameUiModule final : public modules::IUiModule, private framework::uiinput::Owner {
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
		// every listener before the DOM dies (module contract). The token
		// releases first - a stale no-op when the router already reset.
		PageToken.release();
		Page.reset();
		Context = nullptr;
	}

private:
	// framework::uiinput::Owner (menu tier, T06).
	bool claimsKey(int down, int keynum) override {
		(void)down;
		(void)keynum;
		return true;
	}
	bool claimsWheel() override { return true; }
	void onClaimedKeyDown(int keynum) override {
		// Exactly once per physical press (the router suppresses auto-repeat
		// here); the release pairs the just-fed ESC down inside the router.
		if (keynum == K_ESCAPE && Page && Page->isVisible()) {
			// T07.b ESC priority: the first press cancels a running
			// composition instead of closing the page; the next press
			// closes. The IME normally swallows ESC itself (the fork sends
			// an empty SDL_EVENT_TEXT_EDITING), so this is the leak path.
			if (rmlui::textComposing()) {
				rmlui::cancelComposition();
				return;
			}
			Page->hide();
			PageToken.release();
		}
	}
	void onOwnershipLost() override {
		// Force-release (focus loss / teardown): stay one truth - hide the
		// page without touching the (already stale) token.
		if (Page && Page->isVisible())
			Page->hide();
	}

#ifndef NDEBUG
	static void menuPageCommand() {
		if (!Instance || !Instance->Page || !Instance->Context) {
			abc::ConsoleWarn("[AbcUi] menu page not available\n");
			return;
		}
		if (Instance->Page->isVisible()) {
			Instance->Page->hide();
			Instance->PageToken.release();
			abc::ConsolePrint("[AbcUi] menu page hidden\n");
			return;
		}
		Instance->Page->show();
		// Real ownership token (T06): the page claims keys/wheel; mouse
		// reachability rides the router exclusivity like the pointer mode.
		Instance->PageToken = rmlui::uiRouter().acquire(Instance, framework::uiinput::OwnerTier::kMenu);
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
	// Router ownership while the page is shown (T06); the page's visibility
	// mirrors it (onOwnershipLost / onClaimedKeyDown keep them in step).
	framework::uiinput::Token PageToken;
};

GameUiModule* GameUiModule::Instance = nullptr;

// The static object of this TU is the module's owner and destructor; the
// registry only borrows the pointer (coding rule 11).
const GameUiModule RegisterModule;

}

}
