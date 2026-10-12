// gameui feature module - assembly point of the menu replacement domain.
// T03 shipped the empty seam; T05 hung the minimal interactive page off the
// module contract; T06 added the input-ownership owner; T08 made the module
// the INativeMenuSink of the suppression bridge.
//
// T09 completes the loop: the module owns the production MenuController and
// EngineUiBackend. Every menu transition converges here -
//   * the bridge raises/lowers the menu (engine ESC, cold start, focus
//     loss) and the sink syncs the controller (kMenuShown / kMenuHidden),
//   * the page/overlay windows report fixed action intents (resume,
//     disconnect, quit confirm, open stub pages, console placeholder),
//   * the router owner turns ESC into kMenuToggle,
// and after every dispatch syncMenuPresentation() re-renders the document
// set from the controller state (never from DOM inspection), while the
// backend executes the typed actions (resume through the proven BaseUI
// hide path, disconnect/quit as the verbatim original command strings,
// singleplayer pause parity on the in-game transitions).
#include "../Module.h"

#include <RmlUi/Core/Context.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <keydefs.h>

#include "../../framework/Localization.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../rmlui/VGUI2ExtensionImport.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "MenuController.h"

namespace gameui {

namespace {

// The production backend: typed actions in, engine calls out, every engine
// touchpoint behind a thin thunk (the offline backend test injects its own
// recordings through the same Seams shape).
services::EngineUiBackend& menuBackend() {
	static services::EngineUiBackend backend = [] {
		services::EngineUiBackend created;
		services::EngineUiBackend::Seams seams;
		seams.clientCmd = [](const char* command) { abc::EngineClientCmd(command); };
		seams.hideGameUi = []() { return vgui2ext::hideGameUi(); };
		seams.maxClients = []() { return gEngfuncs.GetMaxClients(); };
		seams.bridge = &services::nativeUiBridge();
		created.setSeams(seams);
		return created;
	}();
	return backend;
}

using gameui::menu::EventKind;
using gameui::menu::MenuEvent;
using gameui::menu::MenuOverlay;
using gameui::menu::MenuPage;

class GameUiModule final : public modules::IUiModule, private framework::uiinput::Owner, private services::INativeMenuSink {
public:
	GameUiModule()
		: Controller(&menuBackend()) {
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
		// One action seam for every window: fixed action names in, controller
		// intents out (never an original-UI command).
		const auto wire = [this, context](auto& page) {
			page->setActionSink([this](const char* action) { onMenuAction(action); });
			logLoadOutcome(page->load(context));
		};
		Shell = std::make_unique<panels::mainmenu::MainMenuPage>();
		SubStub = std::make_unique<panels::menushared::MenuStubPage>("menusub", "abcui/rml/menustub.rml");
		ConsoleStub = std::make_unique<panels::menushared::MenuStubPage>("menuconsole", "abcui/rml/consolestub.rml");
		ConfirmQuit = std::make_unique<panels::menushared::ConfirmQuitOverlay>();
		wire(Shell);
		wire(SubStub);
		wire(ConsoleStub);
		wire(ConfirmQuit);
		// The takeover may have raised the menu before the context existed
		// (cold-start activation runs ~1.5s ahead of Rml assembly): the
		// pending show lands now, on the first context.
		if (services::nativeUiBridge().menuUp())
			presentMenu();
	}

	void onShutdown() override {
		// Context still alive here: every document closes and drops its
		// listeners before the DOM dies (module contract). The token
		// releases first - a stale no-op when the router already reset.
		PageToken.release();
		Shell.reset();
		SubStub.reset();
		ConsoleStub.reset();
		ConfirmQuit.reset();
		Context = nullptr;
		MenuPresented = false;
		LastStubPage = MenuPage::kMain;
	}

private:
	static void logLoadOutcome(framework::InteractiveDocument::LoadOutcome outcome) {
		using LoadOutcome = framework::InteractiveDocument::LoadOutcome;
		switch (outcome) {
		case LoadOutcome::kLoaded:
			break;
		case LoadOutcome::kEmergencyFallback:
			abc::ConsoleWarn("[AbcUi] menu document degraded to the emergency page\n");
			break;
		case LoadOutcome::kFailed:
			abc::ConsoleWarn("[AbcUi] menu document failed to load entirely (T21 strict path)\n");
			break;
		}
	}

	// services::INativeMenuSink: the bridge decided the menu state; run the
	// controller sync and present or take down the document set. Called from
	// inside engine callbacks - no re-entry into the bridge beyond the
	// explicit requestMenuDown below.
	void onNativeMenuShow() override {
		presentMenu();
	}
	void onNativeMenuHide() override {
		dismissMenu();
	}

	void presentMenu() {
		if (!Shell || !Context || MenuPresented)
			return;
		MenuPresented = true;
		// Real ownership token (T06): the menu claims keys/wheel; mouse
		// reachability rides the router exclusivity like the pointer mode.
		PageToken = rmlui::uiRouter().acquire(this, framework::uiinput::OwnerTier::kMenu);
		if (!PageToken.held())
			abc::ConsoleWarn("[AbcUi] menu ownership token unavailable (router full)\n");
		services::nativeUiBridge().notePageShown();
		// The state machine follows the bridge (idempotent open), then the
		// presentation follows the state machine. The phase reconciles first,
		// so the open lands on the right home (Pause in-game, Main no-map).
		reconcilePhase();
		Controller.dispatch(MenuEvent{EventKind::kMenuShown});
		syncMenuPresentation();
		if (Controller.state().phase == gameui::menu::BackendPhase::kInGame)
			menuBackend().menuShownInGame();  // singleplayer pause parity
		focusLog("presented");
	}

	void dismissMenu() {
		// The bridge lowered the menu from outside the state machine's
		// decision (routed ESC close, engine hide, focus loss): force-clear
		// the controller, hide the documents, release ownership. No unpause
		// here - the simulation only resumes on a decided close
		// (EngineUiBackend::resumeGame), never on a force-release.
		Controller.dispatch(MenuEvent{EventKind::kMenuHidden});
		hideAllMenuDocuments();
		if (MenuPresented) {
			MenuPresented = false;
			PageToken.release();
			services::nativeUiBridge().notePageHidden();
		}
		focusLog("dismissed");
	}

	// Derives the backend phase the controller needs for its ESC decisions
	// from the bridge's level fact (noteLevelLoaded / noteDisconnected - the
	// same "connected" guard the vanilla hide uses). Production has no
	// loading chain yet (T14): the phase lives at kBooting otherwise, so the
	// reconcile covers exactly the kBooting/kInGame/kDisconnected triangle.
	// Ordering contract: presentMenu reconciles BEFORE opening (the open
	// lands on the right home); the owner's ESC reconciles AFTER its toggle
	// - a menu that survived a map load closes on the phase arrival instead
	// of re-opening as Pause from the cleared stack.
	void reconcilePhase() {
		const auto current = Controller.state().phase;
		if (current != gameui::menu::BackendPhase::kBooting
			&& current != gameui::menu::BackendPhase::kInGame
			&& current != gameui::menu::BackendPhase::kDisconnected)
			return;  // connecting/loading/fatal are T14's to reconcile
		const auto target = services::nativeUiBridge().hasLevel()
			? gameui::menu::BackendPhase::kInGame
			: gameui::menu::BackendPhase::kDisconnected;
		if (current == target)
			return;
		Controller.dispatch(gameui::menu::makePhaseChanged(NextPhaseSession++, target));
	}

	// The single render-from-state function: document visibility and the
	// shell section follow the controller; nothing here infers game state
	// from the DOM (T09 step 1).
	void syncMenuPresentation() {
		if (!Shell || !Context)
			return;
		const auto& state = Controller.state();
		if (!Controller.isMenuActive()) {
			hideAllMenuDocuments();
			return;
		}
		// GameMenu.res conditions ("notsingle"): the browser/create/list row
		// follows the maxplayers fact, not any DOM observation.
		const bool multiplayer = menuBackend().maxClientsFact() > 1;
		Shell->setEntryVisible("servers", multiplayer);
		Shell->setEntryVisible("creategame", multiplayer);
		Shell->setEntryVisible("playerlist", multiplayer);

		// Page layer: the shell carries the two home sections, every other
		// page lands on the not-yet-implemented stub (parity TODO visible,
		// original windows never opened - T09 step 2).
		bool shellVisible = false;
		if (!state.pages.empty()) {
			const bool overlaysHoldFocus = !state.overlays.empty();
			switch (state.pages.top()) {
			case MenuPage::kMain:
				Shell->showSection(panels::mainmenu::MainMenuPage::Section::kMain, !overlaysHoldFocus);
				shellVisible = true;
				break;
			case MenuPage::kPause:
				Shell->showSection(panels::mainmenu::MainMenuPage::Section::kPause, !overlaysHoldFocus);
				shellVisible = true;
				break;
			default:
				showStubPage(SubStub.get(), state.pages.top(), false);
				break;
			}
		}
		if (shellVisible)
			Shell->show();
		else
			Shell->hide();

		// Overlay layer, top-most last (each show pulls to the context
		// front); idempotent shows neither re-focus nor re-order.
		if (state.overlays.contains(MenuOverlay::kConsole))
			showStubPage(ConsoleStub.get(), MenuPage::kMain, true);
		else
			ConsoleStub->hide();
		if (state.overlays.contains(MenuOverlay::kConfirmQuit))
			ConfirmQuit->show();
		else
			ConfirmQuit->hide();
	}

	// Points a stub window at one unfinished surface; kMain is the console
	// placeholder variant (the real console is T16).
	void showStubPage(panels::menushared::MenuStubPage* stub, MenuPage page, bool console) {
		if (!stub)
			return;
		if (!console && LastStubPage == page && stub->isVisible())
			return;  // same surface already presented: no text churn
		LastStubPage = page;
		using Resolve = framework::Localization;
		std::string title, body;
		const char* titleToken = "#MainMenu_StubTitle";
		const char* bodyToken = "#MainMenu_StubSettings";
		switch (page) {
		case MenuPage::kServers:
			bodyToken = "#MainMenu_StubServers";
			break;
		case MenuPage::kCreateGame:
			bodyToken = "#MainMenu_StubCreateGame";
			break;
		case MenuPage::kPlayerList:
			bodyToken = "#MainMenu_StubPlayerList";
			break;
		case MenuPage::kLoading:
			bodyToken = "#MainMenu_StubLoading";
			break;
		case MenuPage::kMain:
			bodyToken = "#MainMenu_StubConsole";
			break;
		default:
			break;  // settings: the default tokens
		}
		Resolve::translate(title, titleToken + 1);
		Resolve::translate(body, bodyToken + 1);
		stub->showFor(title.c_str(), body.c_str());
		stub->show();
	}

	// Fixed action intents from the menu windows (page-internal constants,
	// never user strings - T04 step 2). After the dispatch the presentation
	// re-syncs; a decided close lowers the bridge through the backend's
	// resume path (sink dismissal), never through a second truth here.
	void onMenuAction(const char* action) {
		const std::string intent = action ? action : "";
		if (intent == "resume")
			Controller.dispatch(MenuEvent{EventKind::kResume});
		else if (intent == "disconnect")
			Controller.dispatch(MenuEvent{EventKind::kDisconnectRequested});
		else if (intent == "quit")
			Controller.dispatch(MenuEvent{EventKind::kQuitRequested});
		else if (intent == "quit-yes")
			Controller.dispatch(gameui::menu::makeQuitResolved(true));
		else if (intent == "quit-no")
			Controller.dispatch(gameui::menu::makeQuitResolved(false));
		else if (intent == "settings")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kSettings));
		else if (intent == "servers")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kServers));
		else if (intent == "creategame")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kCreateGame));
		else if (intent == "playerlist")
			Controller.dispatch(gameui::menu::makeOpenPage(MenuPage::kPlayerList));
		else if (intent == "console")
			Controller.dispatch(MenuEvent{EventKind::kConsoleToggle});
		else if (intent == "return" || intent == "close")
			Controller.dispatch(MenuEvent{EventKind::kBack});
		else {
			abc::ConsoleWarn("[AbcUi] unknown menu action \"%s\" dropped\n", intent.c_str());
			return;
		}
		syncMenuPresentation();
		if (!Controller.isMenuActive())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

	void hideAllMenuDocuments() {
		if (Shell)
			Shell->hide();
		if (SubStub)
			SubStub->hide();
		if (ConsoleStub)
			ConsoleStub->hide();
		if (ConfirmQuit)
			ConfirmQuit->hide();
	}

	void focusLog(const char* phase) {
#ifndef NDEBUG
		if (Context) {
			if (Rml::Element* focus = Context->GetFocusElement())
				abc::ConsolePrint("[AbcUi] menu %s, focus on '%s'\n", phase, focus->GetId().c_str());
			else
				abc::ConsoleWarn("[AbcUi] menu %s, no focus element\n", phase);
		}
#else
		(void)phase;
#endif
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
		if (keynum != K_ESCAPE || !MenuPresented)
			return;
		// T07.b ESC priority: the first press cancels a running composition
		// instead of navigating; the next press toggles. The IME normally
		// swallows ESC itself, so this is the leak path.
		if (rmlui::textComposing()) {
			rmlui::cancelComposition();
			return;
		}
		// The state machine owns the ESC semantics: pop one overlay, return
		// from a sub page, or close+resume on the pause home (T04 table).
		// The phase reconciles AFTER the toggle: a menu that survived a map
		// load (stale no-map phase) closes on the phase arrival instead of
		// re-opening as Pause from the cleared stack.
		Controller.dispatch(MenuEvent{EventKind::kMenuToggle});
		reconcilePhase();
		syncMenuPresentation();
		if (!Controller.isMenuActive())
			services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}
	void onOwnershipLost() override {
		// Force-release (focus loss / teardown): the bridge state must follow
		// so a later ESC cannot route into an empty router. The dismissal
		// clears the controller and the documents in one place.
		services::nativeUiBridge().requestMenuDown(services::NativeUiOrigin::kOwnUi);
	}

#ifndef NDEBUG
	static void menuPageCommand() {
		if (!Instance || !Instance->Shell || !Instance->Context) {
			abc::ConsoleWarn("[AbcUi] menu not available\n");
			return;
		}
		auto& bridge = services::nativeUiBridge();
		if (Instance->MenuPresented) {
			bridge.requestMenuDown(services::NativeUiOrigin::kOwnUi);
			abc::ConsolePrint("[AbcUi] menu hidden (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
			return;
		}
		// Diagnostic raise: the same sink path the takeover drives (this is
		// the T05/T06 dev seam, not a second truth).
		bridge.requestMenuUp(services::NativeUiOrigin::kOwnUi);
		abc::ConsolePrint("[AbcUi] menu shown (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
	}
#endif

	static GameUiModule* Instance;

	Rml::Context* Context = nullptr;
	gameui::menu::MenuController Controller;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
	std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
	std::unique_ptr<panels::menushared::MenuStubPage> ConsoleStub;
	std::unique_ptr<panels::menushared::ConfirmQuitOverlay> ConfirmQuit;
	// Router ownership while the menu is presented (T06); the document set
	// mirrors it (dismissal / onOwnershipLost keep them in step).
	framework::uiinput::Token PageToken;
	// Whether THIS module presented the menu (vs. the bridge being up before
	// Rml was ready). Mirrors the bridge's PageShown.
	bool MenuPresented = false;
	// Monotonic phase-event sessions for reconcilePhase (the controller
	// drops anything below its accepted session).
	uint32_t NextPhaseSession = 1;
	// Last surface the sub stub rendered (text churn guard only).
	MenuPage LastStubPage = MenuPage::kMain;
};

GameUiModule* GameUiModule::Instance = nullptr;

// The static object of this TU is the module's owner and destructor; the
// registry only borrows the pointer (coding rule 11).
const GameUiModule RegisterModule;

}
}
