// gameui feature module - assembly point of the menu replacement domain.
// T03 shipped the empty seam; T05 hangs the minimal interactive page's
// lifetime off the module contract (documents die with the context, so the
// page is built in onContextReady and released in onShutdown while the
// context still exists).
//
// T06 added the input-ownership owner (menu tier) for the diagnostic toggle.
// T08 upgrades the module into the production side of the suppression
// bridge: it is the INativeMenuSink - when the takeover raises the menu
// (cold-start ActivateGameUI, ESC, console activation) the module shows the
// minimal page and takes the menu-tier input token; when the bridge lowers
// the menu (routed ESC close, engine hide, focus loss) the module hides the
// page and releases the token. The page's own close path (ESC handled by the
// owner action) keeps the two sides in step by calling requestMenuDown.
//
// The splash page behind the takeover is the T05 mainmenu document standing
// in for every page the later tasks build (T09+: Main/Pause/Settings/...).
// Per the plan: with no complete replacement page yet, the takeover leads
// everything to this minimal page and never shows the original UI.
#include "../Module.h"

#include <RmlUi/Core/Context.h>

#include <abc/Console.h>

#include <keydefs.h>

#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../services/GameUiBridge.h"

#ifndef NDEBUG
#include <abc/CvarCmd.h>
#endif

namespace gameui {

namespace {

class GameUiModule final : public modules::IUiModule, private framework::uiinput::Owner, private services::INativeMenuSink {
public:
	GameUiModule() {
		modules::registry().add(this);
		services::nativeUiBridge().setMenuSink(this);
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
				abc::ConsolePrint("[AbcUi] menu page loaded (shown by the suppression bridge)\n");
				break;
			case LoadOutcome::kEmergencyFallback:
				abc::ConsoleWarn("[AbcUi] menu page degraded to the emergency document\n");
				break;
			case LoadOutcome::kFailed:
				abc::ConsoleWarn("[AbcUi] menu page failed to load entirely (T21 strict path)\n");
				break;
		}
		// The takeover may have raised the menu before the context existed
		// (cold-start activation is ~1.5s ahead of Rml assembly): the pending
		// show lands now, on the first context.
		if (services::nativeUiBridge().menuUp())
			presentPage();
	}

	void onShutdown() override {
		// Context still alive here: the page closes its document and drops
		// every listener before the DOM dies (module contract). The token
		// releases first - a stale no-op when the router already reset.
		PageToken.release();
		Page.reset();
		Context = nullptr;
		PagePresented = false;
	}

private:
	// services::INativeMenuSink: the bridge decided the menu state; present
	// or take down the minimal page. Runs inside engine callbacks - keep it
	// allocation-free beyond what the page itself does on the first show.
	void onNativeMenuShow() override {
		presentPage();
	}
	void onNativeMenuHide() override {
		if (PagePresented && Page && Page->isVisible())
			Page->hide();
		if (PagePresented) {
			PagePresented = false;
			PageToken.release();
			services::nativeUiBridge().notePageHidden();
		}
	}

	void presentPage() {
		// Rml not assembled yet (cold-start activation): the show is
		// remembered by the bridge state itself; onContextReady re-checks it.
		if (!Page || !Context || PagePresented)
			return;
		Page->show();
		PagePresented = true;
		// Real ownership token (T06): the page claims keys/wheel; mouse
		// reachability rides the router exclusivity like the pointer mode.
		PageToken = rmlui::uiRouter().acquire(this, framework::uiinput::OwnerTier::kMenu);
		services::nativeUiBridge().notePageShown();
		if (Rml::Element* focus = Context->GetFocusElement())
			abc::ConsolePrint("[AbcUi] menu page presented, focus on '%s'\n", focus->GetId().c_str());
		else
			abc::ConsoleWarn("[AbcUi] menu page presented, no focus element\n");
	}

	// framework::uiinput::Owner (menu tier, T06/T07 semantics preserved).
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
			onNativeMenuHide(); // one truth: hide + release + bridge state
			// The bridge was up while the page closed by its own action -
			// lower it so the engine answers flip back together with the
			// page disappearing.
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
		}
	}
	void onOwnershipLost() override {
		// Force-release (focus loss / teardown): stay one truth - hide the
		// page without touching the (already stale) token. The bridge state
		// must follow so a later ESC cannot route into an empty router.
		if (PagePresented && Page && Page->isVisible())
			Page->hide();
		if (PagePresented) {
			PagePresented = false;
			services::nativeUiBridge().notePageHidden();
		}
		services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

#ifndef NDEBUG
	static void menuPageCommand() {
		if (!Instance || !Instance->Page || !Instance->Context) {
			abc::ConsoleWarn("[AbcUi] menu page not available\n");
			return;
		}
		auto& bridge = services::nativeUiBridge();
		if (Instance->Page->isVisible()) {
			Instance->onNativeMenuHide();
			bridge.requestMenuDown(services::NativeUiOrigin::kOwnUi);
			abc::ConsolePrint("[AbcUi] menu page hidden (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
			return;
		}
		// Diagnostic toggle: drives the same state transitions the takeover
		// drives (this is the T05/T06 dev seam, not a second truth).
		bridge.requestMenuUp(services::NativeUiOrigin::kOwnUi);
		abc::ConsolePrint("[AbcUi] menu page shown (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
	}
#endif

	static GameUiModule* Instance;

	Rml::Context* Context = nullptr;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Page;
	// Router ownership while the page is shown (T06); the page's visibility
	// mirrors it (onOwnershipLost / onClaimedKeyDown keep them in step).
	framework::uiinput::Token PageToken;
	// Whether THIS module presented the page (vs. the page never having been
	// shown because Rml was not ready). Mirrors the bridge's PageShown.
	bool PagePresented = false;
};

GameUiModule* GameUiModule::Instance = nullptr;

// The static object of this TU is the module's owner and destructor; the
// registry only borrows the pointer (coding rule 11).
const GameUiModule RegisterModule;

}

}
