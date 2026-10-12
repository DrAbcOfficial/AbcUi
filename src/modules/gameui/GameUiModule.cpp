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
#include <RmlUi/Debugger.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <abc/Paths.h>

#include <keydefs.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../framework/Localization.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../rmlui/InputBridge.h"
#include "../../rmlui/TextInput.h"
#include "../../rmlui/VGUI2ExtensionImport.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "../../services/settings/KeyBindModel.h"
#include "../../services/settings/SettingsModel.h"
#include "../../services/settings/VideoModeModel.h"
#include "../../services/settings/VideoPendingStore.h"
#include "MenuController.h"

#include <chrono>

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

// The production cvar seams for the settings model (T10): reads through the
// cvar pointer, writes through the engine Cvar_SetValue API - never a
// concatenated "name value" command string.
services::settings::SettingsModel::Seams settingsSeams() {
	services::settings::SettingsModel::Seams seams;
	seams.exists = [](const char* name) { return abc::CVAR_GET_POINTER(name) != nullptr; };
	seams.readFloat = [](const char* name) { return abc::CVAR_GET_FLOAT(name); };
	seams.writeFloat = [](const char* name, float value) {
		if (!abc::CVAR_GET_POINTER(name))
			return false;
		abc::CVAR_SET_FLOAT(name, value);
		return true;
	};
	return seams;
}

// The production bind seams (T11): reads through IGameUIFuncs (the same
// key table the bind command writes - hw.dll evidence in KeyBindModel.h),
// writes as verbatim `bind`/`unbind` commands through pfnClientCmd. The
// engine command buffer flushes between frames; the model's kQueued/verify
// pair covers that beat.
services::settings::KeyBindModel::Seams bindSeams() {
	services::settings::KeyBindModel::Seams seams;
	seams.bindingForKey = [](int keynum) -> const char* { return vgui2ext::keyBindingForKey(keynum); };
	seams.keyNameForKey = [](int keynum) -> const char* { return vgui2ext::keyNameForKey(keynum); };
	seams.keyStringToKeyNum = [](const char* name) { return vgui2ext::keyStringToKeyNum(name); };
	seams.clientCmd = [](const char* command) {
		abc::EngineClientCmd(command);
		return true; // handed to the engine buffer; truth rides the re-read
	};
	return seams;
}

// T12: the deferred `_restart`. Live-caught on the shipped engine: a
// `_restart` that shares the command flush with the `_setvideomode` writes
// it is meant to apply lets the in-process video re-init come up at the OLD
// mode (the engine re-initializes the display from its startup snapshot, not
// the freshly written registry values) - while the same writes followed by a
// `_restart` on a LATER flush (the shipped page's own two-step behaviour, and
// the raw-console repro) apply correctly, three live runs each way. The
// production thunk therefore holds the restart for the next module frame:
// the model still issues it as the last command of its transaction (offline
// semantics unchanged), only the physical engine emission is deferred one
// frame so the writes land in their own flush first.
bool DeferredVideoRestart = false;

// A confirmation raise queued by the same transaction that (re)starts the
// video: presenting the page touches the Rml context, the bridge and the
// document set - none of that may run in the same frame the engine is asked
// to tear down and re-initialize the display (live-caught: an immediate
// raise after the restart command left the process windowless). The frame
// drain raises it once Context is back.
bool DeferredVideoConfirmRaise = false;

// The production video seams (T12): actual-mode/list reads through the
// IGameUIFuncs vtable slots 5/6/7 (vgui2ext thunks), writes as the verbatim
// engine commands through pfnClientCmd, the pending record through
// abc::Config's atomic JSON writer, clocks through chrono.
services::settings::VideoModeModel::Seams videoSeams() {
	services::settings::VideoModeModel::Seams seams;
	seams.actualMode = [](services::settings::VideoMode& out) {
		return vgui2ext::currentVideoMode(out.Width, out.Height, out.Bpp, out.Windowed);
	};
	seams.modeList = [](services::settings::VideoMode* out, int max) {
		vgui2ext::video::Mode buffer[services::settings::VideoModeModel::kMaxModes];
		if (max > services::settings::VideoModeModel::kMaxModes)
			max = services::settings::VideoModeModel::kMaxModes;
		const int count = vgui2ext::videoModeList(buffer, max);
		for (int i = 0; i < count; ++i) {
			out[i].Width = buffer[i].Width;
			out[i].Height = buffer[i].Height;
			out[i].Bpp = buffer[i].Bpp;
		}
		return count;
	};
	seams.clientCmd = [](const char* command) {
		if (command && std::strcmp(command, "_restart\n") == 0) {
			DeferredVideoRestart = true; // never shares the flush with its writes
			return true;
		}
		abc::EngineClientCmd(command);
		return true; // handed to the engine buffer (same contract as bind writes)
	};
	seams.savePending = [](const services::settings::VideoPendingRecord& record) {
		return services::settings::VideoPendingStore::save(record);
	};
	seams.loadPending = [](services::settings::VideoPendingRecord& out) {
		return services::settings::VideoPendingStore::load(out);
	};
	seams.monotonicSeconds = [] {
		using clock = std::chrono::steady_clock;
		static const auto start = clock::now();
		return std::chrono::duration<double>(clock::now() - start).count();
	};
	seams.wallMs = [] {
		using clock = std::chrono::system_clock;
		return static_cast<long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
				.count());
	};
	return seams;
}

// Mod-dir text file (kb_act.lst / kb_def.lst / valve_english.txt), decoded
// from its shipped encoding (valve_english.txt is UTF-16LE - see
// KeyBindModel::decodeResourceText). Empty on any failure - the bind model
// treats missing sources as fail-closed.
std::string readModFile(const char* relativePath) {
	FILE* file = nullptr;
	if (fopen_s(&file, abc::ModPath(relativePath).c_str(), "rb") != 0 || !file)
		return {};
	std::string raw;
	char buffer[4096];
	size_t got = 0;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		raw.append(buffer, got);
	fclose(file);
	return services::settings::KeyBindModel::decodeResourceText(raw.data(), raw.size());
}

using gameui::menu::EventKind;
using gameui::menu::MenuEvent;
using gameui::menu::MenuOverlay;
using gameui::menu::MenuPage;

class GameUiModule final : public modules::IUiModule, private framework::uiinput::Owner, private services::INativeMenuSink {
public:
	GameUiModule()
		: Controller(&menuBackend()), SettingsModel(services::settings::FirstBatchRows,
			  services::settings::FirstBatchCount),
		  CaptureOwner(this) {
		SettingsModel.setSeams(settingsSeams());
		BindModel.setSeams(bindSeams());
		VideoModel.setSeams(videoSeams());
		modules::registry().add(this);
		services::nativeUiBridge().setMenuSink(this);
		Instance = this;
	}

	const char* name() const override { return "gameui"; }

	void onInit() override {
		// T11 bind sources live in the mod dir (engine-owned data, not abcui
		// assets): the action list, the factory defaults and the label table.
		// Missing files degrade to an empty action table - the settings page
		// renders the bind section as unavailable (fail-closed).
		if (!BindModel.loadActions(readModFile("gfx/shell/kb_act.lst").c_str()))
			abc::ConsoleWarn("[AbcUi] kb_act.lst malformed - key bindings unavailable\n");
		BindModel.loadDefaults(readModFile("gfx/shell/kb_def.lst").c_str());
		BindModel.loadLabels(readModFile("resource/valve_english.txt").c_str());
#ifndef NDEBUG
		// HUD_Init guarded block only (plan 08 §4.9 red line).
		abc::ADD_COMMAND("abc_ui_menu_page", menuPageCommand);
		abc::ADD_COMMAND("abc_ui_bind_status", bindStatusCommand);
		abc::ADD_COMMAND("abc_ui_video_status", videoStatusCommand);
		abc::ADD_COMMAND("abc_ui_video_scroll", videoScrollCommand);
		abc::ADD_COMMAND("abc_ui_video_stage", videoStageCommand);
		abc::ADD_COMMAND("abc_ui_video_apply", videoApplyCommand);
		abc::ADD_COMMAND("abc_ui_video_keep", videoKeepCommand);
		abc::ADD_COMMAND("abc_ui_video_revert", videoRevertCommand);
		abc::ADD_COMMAND("abc_ui_video_window", videoWindowCommand);
#endif
	}

	void onFrame(double frameTime) override {
		(void)frameTime;
		// T12: drain the deferred video restart FIRST and independently of
		// menu presentation - the transaction owns it, not the page (see the
		// DeferredVideoRestart note above).
		const bool restartDrained = DeferredVideoRestart;
		if (DeferredVideoRestart) {
			DeferredVideoRestart = false;
			abc::EngineClientCmd("_restart\n");
		}
		// The queued confirmation raise never shares a frame with a restart
		// drain (see the DeferredVideoConfirmRaise note): one frame apart.
		if (!restartDrained && DeferredVideoConfirmRaise && Context && VideoConfirm) {
			DeferredVideoConfirmRaise = false;
			raiseVideoConfirmation();
		}
		if (!MenuPresented || !Shell || !Context)
			return;
		// T11 bind verification beat: pfnClientCmd lands in the engine
		// command buffer, which flushes between frames. kQueued rows resolve
		// on the first tick after Apply; a change re-projects the page once.
		if (SettingsPresented && Settings && BindModel.hasQueued()) {
			if (BindModel.verify())
				Settings->render();
		}
		// T12 confirmation beat: the monotonic deadline runs on real time.
		// The tick fires the auto-revert even when the window is hidden or
		// unfocused - the deadline never extends. A revert/auto-resolve
		// re-projects the dialog (or closes it) once.
		if (VideoModel.awaitingConfirm()) {
			const bool reverted = VideoModel.tick();
			tickVideoTimeout();
			if (reverted || !VideoModel.awaitingConfirm())
				syncVideoConfirmPage();
			else if (VideoConfirm)
				VideoConfirm->render();
		}
		// Per-frame focus guard (T09 step 5): while the menu is presented,
		// the context focus must stay inside one of the menu documents.
		// Foreign holders exist (the Rml debug hook document grabs the focus
		// whenever the last menu document hides - live-verified), and a
		// sync-time guard alone loses to thieves that act between frames.
		// The debugger's own windows are exempt (a developer using F12 must
		// keep their focus).
		if (Rml::Debugger::IsVisible())
			return;  // a developer using the F12 tools keeps their focus
		Rml::Element* focus = Context->GetFocusElement();
		if (focus) {
			Rml::ElementDocument* owner = focus->GetOwnerDocument();
			// Only a VISIBLE menu document holds legitimate focus - a hidden
			// one (stub/overlay just dismissed) is exactly the stale state
			// this guard exists to repair.
			if (owner && owner->IsVisible()
				&& (owner == Shell->document() || owner == (SubStub ? SubStub->document() : nullptr)
					|| owner == (ConsoleStub ? ConsoleStub->document() : nullptr)
					|| owner == (Settings ? Settings->document() : nullptr)
					|| owner == (ConfirmQuit ? ConfirmQuit->document() : nullptr)
					|| owner == (VideoConfirm ? VideoConfirm->document() : nullptr)))
				return;
		}
		const auto& state = Controller.state();
		// T12: the display-mode confirmation outranks every other surface -
		// it holds the safe default focus while it is up.
		if (VideoConfirmPresented && VideoConfirm) {
			VideoConfirm->refocusDefault();
			return;
		}
		if (state.overlays.contains(MenuOverlay::kConfirmQuit) && ConfirmQuit)
			ConfirmQuit->refocusDefault();
		else if (state.overlays.contains(MenuOverlay::kConsole) && ConsoleStub)
			ConsoleStub->refocusDefault();
		else if (!state.pages.empty() && Shell) {
			if (state.pages.top() == MenuPage::kMain || state.pages.top() == MenuPage::kPause) {
				Shell->showSection(state.pages.top() == MenuPage::kMain
					? panels::mainmenu::MainMenuPage::Section::kMain
					: panels::mainmenu::MainMenuPage::Section::kPause, true);
			} else if (state.pages.top() == MenuPage::kSettings && Settings) {
				Settings->refocusDefault();
			} else if (SubStub) {
				SubStub->refocusDefault();
			}
		}
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
		Settings = std::make_unique<panels::settings::SettingsPage>();
		Settings->setModel(&SettingsModel);
		Settings->setBindModel(&BindModel);
		Settings->setVideoModel(&VideoModel);
		VideoConfirm = std::make_unique<panels::settings::VideoConfirmPage>();
		VideoConfirm->setVideoModel(&VideoModel);
		ConfirmQuit = std::make_unique<panels::menushared::ConfirmQuitOverlay>();
		wire(Shell);
		wire(SubStub);
		wire(ConsoleStub);
		wire(Settings);
		wire(VideoConfirm);
		wire(ConfirmQuit);
		// T12 startup recovery: a persisted unconfirmed-switch record drives
		// the first presentation (confirmation window on the actual mode, or
		// an automatic write-back of the old values). Runs once per engine
		// session, before any menu may be raised.
		resolveVideoRecovery();
		// The takeover may have raised the menu before the context existed
		// (cold-start activation runs ~1.5s ahead of Rml assembly): the
		// pending show lands now, on the first context.
		if (services::nativeUiBridge().menuUp())
			presentMenu();
	}

	void onShutdown() override {
		// Context still alive here: every document closes and drops its
		// listeners before the DOM dies (module contract). The tokens
		// release first - a stale no-op when the router already reset.
		CaptureToken.release();
		PageToken.release();
		Shell.reset();
		SubStub.reset();
		ConsoleStub.reset();
		Settings.reset();
		VideoConfirm.reset();
		ConfirmQuit.reset();
		Context = nullptr;
		MenuPresented = false;
		SettingsPresented = false;
		VideoConfirmPresented = false;
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

	// T11 capture ownership (kCapture tier sits above the menu page). Every
	// claimed edge is swallowed from the bindings; the InputBridge clamp
	// silences the Rml dispatch so a captured key can neither scroll the
	// list, re-click the button under the cursor, nor type anywhere.
	class KeyCaptureOwner final : public framework::uiinput::Owner {
	public:
		explicit KeyCaptureOwner(GameUiModule* owner)
			: Owner(owner) {}
		bool claimsKey(int down, int keynum) override {
			(void)down;
			(void)keynum;
			return true;
		}
		bool claimsWheel() override { return true; }
		void onClaimedKeyDown(int keynum) override { Owner->onCapturedKeyEdge(keynum); }
		bool onClaimedWheelPulse(int keynum) override {
			Owner->onCapturedKeyEdge(keynum);
			return true; // the pulse is consumed - no page scroll
		}
		void onOwnershipLost() override { Owner->onKeyCaptureForceLost(); }

	private:
		GameUiModule* Owner;
	};

	void startKeyCapture() {
		if (!BindModel.capturing() || CaptureToken.held())
			return;
#ifndef NDEBUG
		abc::ConsolePrint("[AbcUi] capture start row=%d\n", BindModel.captureRow());
#endif
		rmlui::setKeyCaptureClamp(true);
		CaptureToken = rmlui::uiRouter().acquire(&CaptureOwner, framework::uiinput::OwnerTier::kCapture);
		if (!CaptureToken.held()) {
			// Unrepresentable depth (8 owners): fail closed - no session.
			BindModel.cancelCapture();
			rmlui::setKeyCaptureClamp(false);
			abc::ConsoleWarn("[AbcUi] capture token unavailable (router full)\n");
		}
	}

	void endKeyCapture() {
		CaptureToken.release();
		rmlui::setKeyCaptureClamp(false);
	}

	// The one captured edge of the session (router dedups auto-repeat).
	// ESC and unknown keys end the session without a draft change; a
	// conflict parks in the model for the page's confirmation layer.
	void onCapturedKeyEdge(int keynum) {
		const auto result = BindModel.captureKey(keynum);
#ifndef NDEBUG
		abc::ConsolePrint("[AbcUi] capture key=%d result=%d conflict=%d\n", keynum,
			static_cast<int>(result), BindModel.conflictPending() ? 1 : 0);
#endif
		endKeyCapture(); // exactly one edge per session, whatever it was
		if (Settings)
			Settings->render();
	}

	// Force-release (focus loss / teardown): drop the session draft-free.
	void onKeyCaptureForceLost() {
		BindModel.cancelCapture();
		rmlui::setKeyCaptureClamp(false);
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
		// A running capture session ends with it (draft-free, clamp off).
		endKeyCapture();
		BindModel.cancelCapture();
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
		// GameMenu.res conditions, verified against the shipped GameUI
		// (CGameMenu::UpdateMenu 0x1000dbe0): "notsingle" rows hide only
		// in-game+singleplayer - the no-map home always shows them, and the
		// in-game half of the condition IS the page split (Main vs Pause).
		// The player list row (OnlyInGame + notsingle) therefore keys on the
		// maxplayers fact alone on the pause page.
		const bool multiplayer = menuBackend().maxClientsFact() > 1;
		Shell->setEntryVisible("playerlist", multiplayer);

		// Page layer: the shell carries the two home sections; the settings
		// page (T10) is the first real sub page; every other page lands on
		// the not-yet-implemented stub (parity TODO visible, original
		// windows never opened - T09 step 2).
		bool shellVisible = false;
		if (!state.pages.empty()) {
			const bool overlaysHoldFocus = !state.overlays.empty();
			switch (state.pages.top()) {
			case MenuPage::kMain:
			case MenuPage::kPause: {
				shellVisible = true;
				// Show before the section focus pass: Focus() needs the
				// document visible (first presentation has no frame between).
				// The per-frame focus guard (onFrame) re-takes the section
				// default against focus thieves.
				Shell->show();
				const bool overlaysHoldFocus = !state.overlays.empty();
				const auto section = state.pages.top() == MenuPage::kMain
					? panels::mainmenu::MainMenuPage::Section::kMain
					: panels::mainmenu::MainMenuPage::Section::kPause;
				Shell->showSection(section, !overlaysHoldFocus);
				break;
			}
			case MenuPage::kSettings:
				presentSettingsPage();
				break;
			default:
				showStubPage(SubStub.get(), state.pages.top(), false);
				break;
			}
		}
		if (shellVisible)
			SubStub->hide();
		else
			Shell->hide();
		if (state.pages.empty() || state.pages.top() != MenuPage::kSettings) {
			if (Settings)
				Settings->hide();
			SettingsPresented = false;
		}

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
		// T12: the confirmation page is the top-most layer of all - a page
		// sync while it is up must not raise another document over it.
		if (VideoConfirmPresented && VideoConfirm)
			VideoConfirm->show();
	}

	// The settings page (T10/T11): fresh engine truth on every open (untouched
	// rows follow silently, pending drafts survive and surface), then the
	// page projects both models. Re-opening after a force-dismiss keeps the
	// drafts - the models' reopen rules prompt instead of overwriting.
	void presentSettingsPage() {
		if (!Settings)
			return;
		if (!SettingsPresented) {
			SettingsModel.snapshot();
			BindModel.snapshot();
			VideoModel.snapshot(); // fresh actual mode + enumerated list
			Settings->render();
			SettingsPresented = true;
		}
		Settings->show();
	}

	// --- T12 display-mode transaction ------------------------------------
	// Startup recovery (onContextReady): a persisted unconfirmed-switch
	// record either re-opens the confirmation window on the ACTUAL mode or
	// auto-reverts to the old values (see VideoModeModel). Runs once per
	// context; the record survives restarts, so this is the crash path too.
	void resolveVideoRecovery() {
		const auto outcome = VideoModel.resolveRecovery();
		switch (outcome) {
		case services::settings::VideoModeModel::RecoveryResult::kPending:
			abc::ConsoleWarn("[AbcUi] display change awaiting confirmation (%s)\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			break;
		case services::settings::VideoModeModel::RecoveryResult::kAutoReverted:
			abc::ConsoleWarn("[AbcUi] unresolved display change restored to %s\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			break;
		case services::settings::VideoModeModel::RecoveryResult::kAccepted:
			abc::ConsoleWarn("[AbcUi] stale display-change record accepted as kept\n");
			break;
		case services::settings::VideoModeModel::RecoveryResult::kDeferred:
			abc::ConsoleWarn("[AbcUi] display-change record kept (query interface missing)\n");
			break;
		case services::settings::VideoModeModel::RecoveryResult::kNoRecord:
			break;
		}
		if (VideoModel.awaitingConfirm())
			DeferredVideoConfirmRaise = true; // frame drain raises it (restart-safe)
	}

	// Applies the staged video change (Settings "video-apply" intent): the
	// model persists the record, writes the changed commands and restarts
	// the video in-process; the confirmation page is raised immediately (the
	// documents survive the HUD_Shutdown/HUD_Init cycle - the record is the
	// crash truth, these are only views).
	void applyVideoChange() {
		if (!VideoModel.available() || !VideoModel.hasStaged() || VideoModel.awaitingConfirm())
			return;
		if (!VideoModel.apply()) {
			abc::ConsoleWarn("[AbcUi] display switch refused (record or command write failed)\n");
			if (Settings)
				Settings->render();
			return;
		}
		// The restart is queued in the same transaction (deferred thunk) and
		// the confirmation raise rides the frame drain: never from here.
		DeferredVideoConfirmRaise = true;
		if (Settings)
			Settings->render();
	}

	void raiseVideoConfirmation() {
		if (!VideoConfirm || !Context)
			return;
		// The takeover rule: the confirmation outranks everything else in
		// the menu (it is the only way back from a bad mode). If the menu
		// was not up, raise it through the bridge - the takeover keeps the
		// original UI suppressed.
		if (!MenuPresented)
			services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
		// The page covers the whole viewport: hide the other documents while
		// it is up (the sync re-shows the right one when it closes).
		if (Shell)
			Shell->hide();
		if (SubStub)
			SubStub->hide();
		if (ConsoleStub)
			ConsoleStub->hide();
		if (Settings)
			Settings->hide();
		VideoConfirmPresented = true;
		VideoCountdownShown = VideoModel.remainingSeconds();
		VideoConfirm->render();
		VideoConfirm->show();
	}

	void closeVideoConfirmation() {
		if (VideoConfirm)
			VideoConfirm->hide();
		VideoConfirmPresented = false;
		VideoCountdownShown = -1;
		if (MenuPresented)
			syncMenuPresentation();
	}

	void syncVideoConfirmPage() {
		if (!VideoConfirm)
			return;
		if (VideoModel.awaitingConfirm()) {
			// Never raise inside a frame that issued engine commands: queue
			// it for the frame drain (see DeferredVideoConfirmRaise).
			if (!VideoConfirmPresented || !VideoConfirm->isVisible())
				DeferredVideoConfirmRaise = true;
		} else if (VideoConfirmPresented) {
			closeVideoConfirmation();
		}
	}

	// Countdown re-render throttle: once per second (the value only moves on
	// second boundaries; per-frame DOM writes would be churn).
	void tickVideoTimeout() {
		if (!VideoConfirm || !VideoConfirmPresented)
			return;
		const int seconds = VideoModel.remainingSeconds();
		if (seconds == VideoCountdownShown)
			return;
		VideoCountdownShown = seconds;
		VideoConfirm->render();
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
		else if (intent == "bind-capture-begin") {
			// T11: the settings page started a capture session in the bind
			// model; the module raises the kCapture ownership tier above the
			// menu page so the next physical edge is captured, not played.
			startKeyCapture();
			return; // no controller transition - the page state already moved
		}
		else if (intent == "video-apply") {
			applyVideoChange();
			return;
		}
		else if (intent == "video-keep") {
			// Keep the confirmed mode; a refused confirmation stays put (the
			// page re-renders the current truth).
			if (VideoModel.confirm()) {
				abc::ConsolePrint("[AbcUi] display change confirmed (%s)\n",
					services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
				syncVideoConfirmPage();
			} else if (VideoConfirm) {
				VideoConfirm->render();
			}
			return;
		}
		else if (intent == "video-revert") {
			// Restore the previous mode; a refused write keeps the dialog up
			// (the deadline retries every second, the record survives).
			if (VideoModel.revert()) {
				abc::ConsolePrint("[AbcUi] display change reverted to %s\n",
					services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
				syncVideoConfirmPage();
			} else if (VideoConfirm) {
				VideoConfirm->render();
			}
			return;
		}
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
		if (Settings)
			Settings->hide();
		if (ConfirmQuit)
			ConfirmQuit->hide();
		// The video confirmation is NOT hidden by a generic hide: it must
		// stay up until confirmed/reverted (a dismissal cannot silently
		// abandon a mode the user has not confirmed). dismissMenu() calls
		// this, so keep it visibly module-owned.
		SettingsPresented = false;
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
		// T12: an outstanding display confirmation is modal - ESC is consumed
		// (the dialog resolves by button or by its own deadline; a dismissal
		// cannot silently abandon an unconfirmed mode).
		if (VideoModel.awaitingConfirm())
			return;
		// The state machine owns the ESC semantics: pop one overlay, return
		// from a sub page, or close+resume on the pause home (T04 table).
		// The phase reconciles AFTER the toggle: a menu that survived a map
		// load (stale no-map phase) closes on the phase arrival instead of
		// re-opening as Pause from the cleared stack.
		// T10 exception: ESC on a dirty settings page is consumed by the
		// unsaved-changes prompt (second press dismisses it = stay) - the
		// controller's plain pop would silently discard the drafts.
		if (Controller.state().overlays.empty()
			&& Controller.state().pages.topIs(MenuPage::kSettings) && Settings
			&& Settings->isDirty()) {
			Settings->requestLeave();
			syncMenuPresentation();
			return;
		}
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
	// T11 diagnostics: the capture/clamp/router state and every bind row's
	// draft - the live-debugging counterpart of the offline model dumps.
	static void bindStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		unsigned wheelPulses = 0, wheelDelivered = 0;
		rmlui::wheelDiagCounts(wheelPulses, wheelDelivered);
		float panelScroll = -1.0f, panelScrollHeight = -1.0f;
		std::string hoverId = "(none)";
		if (self.Context) {
			if (Rml::Element* hover = self.Context->GetHoverElement())
				hoverId = hover->GetId().empty() ? hover->GetTagName() : hover->GetId();
			if (self.Settings && self.Settings->document()) {
				if (Rml::Element* panel = self.Settings->document()->GetElementById("panel")) {
					panelScroll = panel->GetScrollTop();
					panelScrollHeight = panel->GetScrollHeight();
				}
			}
		}
		abc::ConsolePrint("[AbcUi] binds: reads=%d capturing=%d row=%d conflict=%d token=%d clamp=%d\n",
			self.BindModel.readsAvailable() ? 1 : 0, self.BindModel.capturing() ? 1 : 0,
			self.BindModel.captureRow(), self.BindModel.conflictPending() ? 1 : 0,
			self.CaptureToken.held() ? 1 : 0, rmlui::keyCaptureClampActive() ? 1 : 0);
		abc::ConsolePrint("[AbcUi] wheel: pulses=%u delivered=%u panelScroll=%.1f scrollHeight=%.1f hover=%s\n",
			wheelPulses, wheelDelivered, panelScroll, panelScrollHeight, hoverId.c_str());
		for (const auto& view : self.BindModel.rows()) {
			if (view.header)
				continue;
			if (view.state == services::settings::BindRowState::kClean
				&& view.draftKeyNames.empty())
				continue; // only active rows, keeps the dump readable
			std::string keys;
			for (const auto& key : view.draftKeyNames)
				keys += key + " ";
			abc::ConsolePrint("[AbcUi] bind %d '%s' state=%d keys=[%s]\n", view.actionIndex,
				view.action ? view.action->Command.c_str() : "?", static_cast<int>(view.state),
				keys.c_str());
		}
	}

	// T12 diagnostics: the video seams step-by-step (query reachability,
	// actual mode, enumerated count) without opening any page - bisects the
	// query path from the page path during live debugging.
	static void videoStatusCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		abc::ConsolePrint("[AbcUi] video: available=%d\n", self.VideoModel.available() ? 1 : 0);
		services::settings::VideoMode actual;
		const bool haveActual = vgui2ext::currentVideoMode(actual.Width, actual.Height, actual.Bpp,
			actual.Windowed);
		abc::ConsolePrint("[AbcUi] video: current=%d %s\n", haveActual ? 1 : 0,
			haveActual ? services::settings::VideoModeModel::describe(actual).c_str() : "(unreadable)");
		vgui2ext::video::Mode modes[64];
		const int count = vgui2ext::videoModeList(modes, 64);
		abc::ConsolePrint("[AbcUi] video: modes=%d first=%dx%d\n", count,
			count > 0 ? modes[0].Width : 0, count > 0 ? modes[0].Height : 0);
		self.VideoModel.snapshot();
		abc::ConsolePrint("[AbcUi] video: snapshot actualKnown=%d staged=%d awaiting=%d\n",
			self.VideoModel.actualKnown() ? 1 : 0, self.VideoModel.hasStaged() ? 1 : 0,
			self.VideoModel.awaitingConfirm() ? 1 : 0);
	}

	// T12 diagnostic: scroll the settings panel (the scripted wheel injection
	// is unreliable - T11 record; this only moves the scroll container for
	// screenshot/verification purposes, exactly what the panelScroll field of
	// abc_ui_bind_status reports).
	static void videoScrollCommand() {
		if (!Instance || !Instance->Settings || !Instance->Settings->document()) {
			abc::ConsoleWarn("[AbcUi] settings page not open\n");
			return;
		}
		Rml::Element* panel = Instance->Settings->document()->GetElementById("panel");
		if (!panel) {
			abc::ConsoleWarn("[AbcUi] settings panel missing\n");
			return;
		}
		panel->SetScrollTop(panel->GetScrollHeight());
		abc::ConsolePrint("[AbcUi] video: panel scrollTop=%.1f height=%.1f\n", panel->GetScrollTop(),
			panel->GetScrollHeight());
	}

	// T12 diagnostics: drive the SAME module paths the page intents call
	// (stage is model-direct in the page; apply/keep/revert are the module
	// handlers) - scripted UI clicking is unreliable for the select/confirm
	// buttons, the state machine under test is identical.
	static void videoStageCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		self.VideoModel.snapshot();
		// Args: width height (engine argv through the client engine funcs).
		if (gEngfuncs.Cmd_Argc() < 3) {
			abc::ConsoleWarn("[AbcUi] usage: abc_ui_video_stage <width> <height>\n");
			return;
		}
		const int w = atoi(gEngfuncs.Cmd_Argv(1));
		const int h = atoi(gEngfuncs.Cmd_Argv(2));
		if (w <= 0 || h <= 0) {
			abc::ConsoleWarn("[AbcUi] bad dimensions\n");
			return;
		}
		if (!self.VideoModel.stageResolution(w, h)) {
			abc::ConsoleWarn("[AbcUi] stage refused (not an enumerated mode)\n");
			return;
		}
		abc::ConsolePrint("[AbcUi] video staged: %s\n",
			services::settings::VideoModeModel::describe(self.VideoModel.stagedTarget()).c_str());
		if (self.Settings && self.SettingsPresented)
			self.Settings->render();
	}

	static void videoApplyCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		Instance->applyVideoChange();
		abc::ConsolePrint("[AbcUi] video apply issued (awaiting=%d)\n",
			Instance->VideoModel.awaitingConfirm() ? 1 : 0);
	}

	static void videoKeepCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.VideoModel.confirm()) {
			abc::ConsolePrint("[AbcUi] video kept: %s\n",
				services::settings::VideoModeModel::describe(self.VideoModel.actual()).c_str());
			self.syncVideoConfirmPage();
		} else {
			abc::ConsoleWarn("[AbcUi] keep refused (kind=%d)\n",
				static_cast<int>(self.VideoModel.confirmKind()));
			self.syncVideoConfirmPage();
		}
	}

	// T12 diagnostic: override the in-session confirmation window (seconds).
	// Debug-only transport accommodation - the CLI's RCON channel rebuild
	// after `_restart` can outlast the production 15s window, so keep/reject
	// paths cannot be commanded before the auto-revert fires. 0 restores the
	// production constant.
	static void videoWindowCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		int seconds = 0;
		if (gEngfuncs.Cmd_Argc() >= 2)
			seconds = atoi(gEngfuncs.Cmd_Argv(1));
		Instance->VideoModel.setConfirmWindowMsForTesting(seconds > 0 ? seconds * 1000 : 0);
		abc::ConsolePrint("[AbcUi] video confirm window: %ds\n", seconds > 0 ? seconds : 15);
	}

	static void videoRevertCommand() {
		if (!Instance) {
			abc::ConsoleWarn("[AbcUi] no module instance\n");
			return;
		}
		GameUiModule& self = *Instance;
		if (self.VideoModel.revert())
			abc::ConsolePrint("[AbcUi] video reverted\n");
		else
			abc::ConsoleWarn("[AbcUi] revert refused/failed\n");
		self.syncVideoConfirmPage();
	}

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
	// T10/T11 settings domain: the description-table models (engine truth
	// via the cvar/bind seams) and their page projections. The models
	// outlive the documents - drafts survive a force-dismiss and re-surface
	// on the next open.
	services::settings::SettingsModel SettingsModel;
	services::settings::KeyBindModel BindModel;
	// T12 display-mode transaction: engine reads/writes + the persisted
	// unconfirmed-switch record. Lives across HUD cycles like the other
	// models (the record file is the crash truth; this is the cache).
	services::settings::VideoModeModel VideoModel;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
	std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
	std::unique_ptr<panels::menushared::MenuStubPage> ConsoleStub;
	std::unique_ptr<panels::settings::SettingsPage> Settings;
	std::unique_ptr<panels::settings::VideoConfirmPage> VideoConfirm;
	std::unique_ptr<panels::menushared::ConfirmQuitOverlay> ConfirmQuit;
	// Router ownership while the menu is presented (T06); the document set
	// mirrors it (dismissal / onOwnershipLost keep them in step).
	framework::uiinput::Token PageToken;
	// T11 key-capture session: the kCapture tier owner above the menu page,
	// held exactly while the bind model waits for one physical edge.
	KeyCaptureOwner CaptureOwner;
	framework::uiinput::Token CaptureToken;
	// Whether THIS module presented the menu (vs. the bridge being up before
	// Rml was ready). Mirrors the bridge's PageShown.
	bool MenuPresented = false;
	// Whether the settings page is the presented surface (snapshot-on-open
	// runs once per open, not on every sync).
	bool SettingsPresented = false;
	// T12: whether the confirmation page is up (module-owned truth; the DOM
	// visibility follows it).
	bool VideoConfirmPresented = false;
	// Last countdown seconds value projected into the confirm page (the
	// per-frame tick re-renders only when the shown second moves).
	int VideoCountdownShown = -1;
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
