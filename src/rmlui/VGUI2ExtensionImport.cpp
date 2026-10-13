// vgui2ext implementation - see VGUI2ExtensionImport.h.
#include <metahook.h>

#include "VGUI2ExtensionImport.h"

// RmlUi headers first: the SourceSDK mathlib chain the VGUI/engine interface
// headers pull in defines Assert/FloatEqual as function-like macros
// (tier0/dbg.h), which would break RmlUi's own Debug.h macros if it were
// parsed afterwards. vgui2 extension code that needs both keeps this order.
#include "InputBridge.h"
#include "RmlUiModule.h"
#include "TextInput.h"

#include <ISurface2.h>
#include <vgui/Cursor.h>
#include <IVGUI2Extension.h>
#include <IGameUIFuncs.h>
#include <ILocalize.h>
#include <IBaseUI.h>
#include <keydefs.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "PaintGate.h"
#include "../services/GameUiBridge.h"

namespace vgui2ext {

namespace {

IVGUI2Extension* Extension = nullptr;
// T08: the native-UI callbacks (GameUI/GameConsole) are registered once and
// stay registered for the whole engine session - the takeover must not have
// registration gaps at map changes, so they are NOT tied to the HUD-cycle
// scoped shutdown() above. shutdownNative() removes them at plugin teardown.
IVGUI2Extension* NativeExtension = nullptr;
vgui::ISurface2* Surface2 = nullptr;
IGameUIFuncs* GameUIFuncs = nullptr;
bool WarnedUnavailable = false;
bool UICallbacksRegistered = false;
bool NativeCallbacksRegistered = false;

bool warnUnavailableOnce() {
	if (!WarnedUnavailable) {
		abc::ConsoleWarn("[AbcUi] VGUI2Extension.dll not found - surface textures and mouse polling disabled\n");
		WarnedUnavailable = true;
	}
	return false;
}

// The hook-contract T2 rule: write return values through the proxy-owned
// storage with the REAL type of the wrapped method (int slots via int*,
// bool slots via bool*). A wrong cast silently corrupts the return.
void writePluginInt(VGUI2Extension_CallbackContext* ctx, int value) {
	if (ctx && ctx->pPluginReturnValue)
		*static_cast<int*>(ctx->pPluginReturnValue) = value;
}

void writePluginBool(VGUI2Extension_CallbackContext* ctx, bool value) {
	if (ctx && ctx->pPluginReturnValue)
		*static_cast<bool*>(ctx->pPluginReturnValue) = value;
}

// SDL key-event view for the physical ESC release observation (SDL3-style
// ids, SDL2-style layout - same evidence as TextInput.cpp's text views).
// Observation only: the extension's own handler owns real dispatch; this
// seam never writes Result.
constexpr unsigned kSdlEventKeyDown = 0x300;
constexpr unsigned kSdlEventKeyUp = 0x301;
constexpr int kSdlKeycodeEscape = 0x1B;

struct SdlKeyEvent {
	unsigned Type;
	unsigned Timestamp;
	unsigned WindowId;
	unsigned Which;
	int Scancode;
	int Key;
	unsigned short Mod;
	unsigned short Raw;
	bool Down;
	bool Repeat;
};

void notePhysicalEscUpFromEvent(const void* event) {
	const auto* key = static_cast<const SdlKeyEvent*>(event);
	if (key->Type != kSdlEventKeyUp || key->Key != kSdlKeycodeEscape)
		return;
	services::nativeUiBridge().noteEscPhysicalUp();
}

// R-4 callback path (batch C): the engine surface WndProc sees every UI-level
// mouse message - including injected ones the async-key poll cannot observe
// (GetAsyncKeyState never sees SDL/window-message input). Observation-only:
// the result stays IGNORED, the vanilla surface still handles everything.
class UICallbacks final : public IVGUI2Extension_BaseUICallbacks {
public:
	int GetAltitude() const override { return 0; }
	void Initialize(CreateInterfaceFn* factories, int count) override {
		(void)factories;
		(void)count;
		// T08 step 1: the earliest proven install point (T02 §5: ~1.3s
		// before the first native paint). The gate arms itself only after
		// the IPanel hooks are verified in place; an unsupportable binary
		// stops here (SysError) instead of running with the old UI.
		rmlui::installPaintGate();
	}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion) override {
		(void)engineFuncs;
		(void)interfaceVersion;
	}
	void Shutdown() override {
		// Deliberately empty: at BaseUI::Shutdown vgui2 is already gone
		// (hook-contract §9). The IPanel hooks come down in the GameUI
		// Shutdown notification instead.
	}
	void Key_Event(int& down, int& keynum, const char*& pszCurrentBinding,
		VGUI2Extension_CallbackContext* CallbackContext) override {
		// Second source from T06's table (S6). While the ORIGINAL GameUI held
		// the key destination the engine drove this seam and then forwarded to
		// the client export chain; the takeover keeps the destination from
		// ever activating (the menu queries answer ours and the paint is
		// gated), so this callback stays OBSERVATION-ONLY - feeding a second
		// physical source into routeKey would violate the one-injection-point
		// rule (T06 step 1). What the count proves at acceptance: whether any
		// edge still reaches the vanilla chain while the menu is up.
		(void)down;
		(void)keynum;
		(void)pszCurrentBinding;
		(void)CallbackContext;
		services::nativeUiBridge().noteBaseUiKeyEvent();
	}
	// T07 text seam: the engine forwards raw SDL_Event structs here (the
	// SDL2-based IBaseUI shape - CallEngineSurfaceAppHandler). Text events
	// are consumed at PRE-dispatch only: the proxy repeats every event
	// post-dispatch (07 doc item 7) and a second feed would insert the
	// committed text twice. Observation-only otherwise - the Result is never
	// written, the original surface keeps its own copy of every event.
	void CallEngineSurfaceAppProc(void*& pevent, void*& userData,
		VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)userData;
		if (!CallbackContext->IsPost) {
			// Physical ESC release tracking (one close route per press).
			notePhysicalEscUpFromEvent(pevent);
			rmlui::onSurfaceAppEvent(pevent);
			// T09: the menu-owned keyboard stream - with the takeover active
			// the engine stops feeding HUD_Key_Event, so the surface SDL
			// keys/wheel are the only physical source left; they join the
			// same routeKey injection point (ESC excluded there).
			rmlui::onSurfaceMenuKey(pevent);
		}
	}
	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam,
		VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)CallbackContext;
		rmlui::onSurfaceWndProc(hwnd, msg, wparam, lparam);
	}
	void Paint(int& x, int& y, int& right, int& bottom,
		VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)x;
		(void)y;
		(void)right;
		(void)bottom;
		(void)CallbackContext;
	}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {
		// The outer (engine CBaseUI) layer stays native: its engine-root
		// flips are the necessary side effects the takeover preserves. The
		// inner IGameUI layer is where the suppression happens; this callback
		// is observation only.
		(void)CallbackContext;
		services::nativeUiBridge().noteBaseUiToggle();
	}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)CallbackContext;
		services::nativeUiBridge().noteBaseUiToggle();
	}
	void HideConsole(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
	void ShowConsole(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
};

UICallbacks UICallbacksInstance;

// T14 (defined below with its install/uninstall seams): the GameUI::Start
// notification installs the Sven password-prompt slot hook; the ClientVGUI
// Shutdown seam removes it.
void installPasswordPromptHook();

// T08 entry layer: the inner IGameUI funnel. Everything that opens or closes
// the native menu converges here (the engine's ESC/key handlers and
// ServerBrowser.dll all route through ActivateGameUI/HideGameUI, directly or
// via the IBaseUI methods the extension proxies). While the bridge is armed
// these handlers answer the two queries from the replacement state and
// consume the two actions, so the vanilla CGameUI internals (panel flips,
// setpause/unpause/hideconsole) never run. The outer CBaseUI layer keeps the
// engine-root side effects of the toggle.
//
// Result discipline (hook-contract §3/T1): pre and post are separate
// dispatches with Result reset between them. Every armed callback sets its
// Result in BOTH phases (a pre SUPERCEDE that is not re-asserted in post
// falls back to the original's return value). Observed-only callbacks leave
// Result UNSET.
class NativeGameUICallbacks final : public IVGUI2Extension_GameUICallbacks {
public:
	int GetAltitude() const override { return 0; }

	void Initialize(CreateInterfaceFn* factories, int count) override {
		(void)factories;
		(void)count;
	}
	void PreStart(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {
		(void)engineFuncs;
		(void)interfaceVersion;
		(void)system;
	}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {
		(void)engineFuncs;
		(void)interfaceVersion;
		(void)system;
		// T14: with GameUI.dll mapped and started, the Sven password-prompt
		// slot gets its observation hook (see the install note above).
		installPasswordPromptHook();
	}
	void Shutdown() override {
		// Fallback only: on this extension pin the notification does NOT
		// fire at engine exit (the proxy unhooks its IGameUI slots before
		// the original Shutdown would dispatch it - see the uninstall-seam
		// note at NativeClientVGUICallbacks below). Harmless when it does
		// fire: uninstallPaintGate is idempotent.
		rmlui::uninstallPaintGate();
	}
	void PostShutdown() override {}

	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return; // before the gate exists the vanilla path stays
		// Pre decides once; post re-asserts the same answer (the proxy
		// resets Result between the phases).
		if (!CallbackContext->IsPost)
			bridge.engineActivate();
		writePluginInt(CallbackContext, 1); // vanilla ActivateGameUI returns 1
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}

	void ActivateDemoUI(VGUI2Extension_CallbackContext* CallbackContext) override {
		// Not in scope (no demo UI); vanilla behaviour. Note: this wrapper
		// dispatches post unconditionally (hook-contract T4) - never rely on
		// it for one-shot work.
		(void)CallbackContext;
	}

	void HasExclusiveInput(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		if (!CallbackContext->IsPost)
			bridge.noteExclusiveQuery();
		writePluginInt(CallbackContext, bridge.hasExclusiveInput() ? 1 : 0);
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}

	void RunFrame(VGUI2Extension_CallbackContext* CallbackContext) override {
		// Never suppressed (T08 step 3: the original keeps its necessary
		// background frame work; the paint gate, not this callback, is what
		// keeps its output invisible).
		(void)CallbackContext;
		services::nativeUiBridge().noteRunFrame();
	}

	void ConnectToServer(const char*& game, int& IP, int& port, VGUI2Extension_CallbackContext* CallbackContext) override {
		// Connect is T14/T17 territory; the vanilla window never opens
		// because the menu answers stay ours and the paint gate is armed,
		// but the engine-side connect chain keeps working.
		(void)game;
		(void)IP;
		(void)port;
		(void)CallbackContext;
	}
	void DisconnectFromServer(VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)CallbackContext;
		services::nativeUiBridge().noteDisconnected();
	}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		// NOTE: no IsPost branch - at this pin CGameUIProxy::HideGameUI
		// dispatches pre AND post with IsPost=false in both phases
		// (upstream GameUI.cpp:1669; hook-contract T5). The bridge coalesces
		// the duplicate dispatch AND the held-ESC repeats into one route.
		const auto action = bridge.engineHide();
		if (action == services::ToggleAction::kRouteClose) {
			// One synthetic ESC press through the input router: the page
			// owner keeps its once-per-press semantics (T07: a running
			// composition is cancelled first, the next press closes the
			// page and releases ownership).
			bridge.noteEscRouted();
			rmlui::routeKey(1, K_ESCAPE, false);
			rmlui::routeKey(0, K_ESCAPE, false);
			// The page was shown but no owner could act (torn-down router):
			// lower the state directly instead of leaving a phantom menu.
			if (bridge.menuUp() && !rmlui::uiRouter().exclusive())
				bridge.requestMenuDown(services::NativeUiOrigin::kBaseUi);
		} else if (action == services::ToggleAction::kStateClose) {
			// Menu up without a page (Rml not ready): state-only close.
			bridge.requestMenuDown(services::NativeUiOrigin::kBaseUi);
		}
		// The vanilla CGameUI::HideGameUI never runs while armed: its
		// unpause/hideconsole (T09 decides their replacement) and its panel
		// flips are original-UI behaviour. The outer CBaseUI::HideGameUI
		// still performs the engine-root side effects.
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}
	void IsGameUIActive(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		if (!CallbackContext->IsPost)
			bridge.noteActiveQuery();
		writePluginBool(CallbackContext, bridge.isGameUIActive());
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}

	void LoadingStarted(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override {
		// T14: the engine stream enters the loading feed here - the borrowed
		// pointers are copied before the callback returns (hook-contract T7);
		// the vanilla handler still runs (observed, never superceded): its
		// non-visual bookkeeping stays intact and its dialog stays paint-gated.
		if (CallbackContext->IsPost)
			return;
		services::nativeUiBridge().noteLoadingStart();
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kLoadingStarted;
		event.text = resourceType ? resourceType : "";
		event.text2 = resourceName ? resourceName : "";
		services::loadingFeed().push(event);
	}
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		auto& bridge = services::nativeUiBridge();
		bridge.noteLoadingFinish();
		// A finished level load is the engine-confirmed "in game" fact the
		// vanilla connected-only hide gate reads.
		if (resourceType && !_stricmp(resourceType, "level"))
			bridge.noteLevelLoaded();
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kLoadingFinished;
		event.text = resourceType ? resourceType : "";
		event.text2 = resourceName ? resourceName : "";
		services::loadingFeed().push(event);
	}
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kStartProgress;
		event.text = progressType ? progressType : "";
		event.steps = progressSteps;
		services::loadingFeed().push(event);
	}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kContinueProgress;
		event.point = progressPoint;
		event.fraction = progressFraction;
		services::loadingFeed().push(event);
	}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kStopProgress;
		event.error = bError;
		event.text = failureReason ? failureReason : "";
		event.text2 = extendedReason ? extendedReason : "";
		services::loadingFeed().push(event);
	}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kStatusText;
		event.text = statusText ? statusText : "";
		services::loadingFeed().push(event);
	}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kSecondaryProgress;
		event.fraction = progress;
		services::loadingFeed().push(event);
	}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {
		if (CallbackContext->IsPost)
			return;
		services::LoadingFeedEvent event;
		event.kind = services::LoadingFeedKind::kSecondaryText;
		event.text = statusText ? statusText : "";
		services::loadingFeed().push(event);
	}
	const char* GetControlModuleName() const override { return "AbcUi"; }
};

NativeGameUICallbacks GameUICallbacksInstance;

// T08 step 5: the legacy GameConsole never presents. Activation is consumed
// (the minimal notice page stands in for every not-yet-completed surface;
// the startup -console path is T16), visibility answers false, and its paint
// is blocked by the traversal gate already.
class NativeGameConsoleCallbacks final : public IVGUI2Extension_GameConsoleCallbacks {
public:
	int GetAltitude() const override { return 0; }

	void Activate(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		if (!CallbackContext->IsPost) {
			bridge.noteConsoleActivate();
			// The console is not a page in M1: the minimal page is what a
			// console activation shows while T16 is unimplemented.
			bridge.requestMenuUp(services::NativeUiOrigin::kGameConsole);
		}
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}
	void Initialize(VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)CallbackContext;
	}
	void Hide(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		if (!CallbackContext->IsPost)
			bridge.noteConsoleHide();
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}
	void Clear(VGUI2Extension_CallbackContext* CallbackContext) override {
		// The text buffer may clear; no visual of its own.
		(void)CallbackContext;
	}
	void IsConsoleVisible(VGUI2Extension_CallbackContext* CallbackContext) override {
		auto& bridge = services::nativeUiBridge();
		if (!bridge.armed())
			return;
		if (!CallbackContext->IsPost)
			bridge.noteConsoleVisibleQuery();
		writePluginBool(CallbackContext, bridge.consoleVisible());
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}
	void Printf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {
		// Observe-only for now (T15 consumes the text through the bounded
		// queue). Never print from inside this callback: the extension's own
		// Printf path re-enters it (hook-contract T8).
		(void)str;
		(void)CallbackContext;
	}
	void DPrintf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)str;
		(void)CallbackContext;
	}
	void SetParent(vgui::VPANEL parent, VGUI2Extension_CallbackContext* CallbackContext) override {
		// Parent wiring is engine plumbing; the original keeps it.
		(void)parent;
		(void)CallbackContext;
	}
};

NativeGameConsoleCallbacks GameConsoleCallbacksInstance;

// T08 uninstall seam. hook-contract §9 prescribed the GameUI::Shutdown
// notification, but on this extension pin that notification never fires at
// engine exit: CBaseUIProxy::Shutdown calls GameUI_UninstallHooks() BEFORE
// the original CBaseUI::Shutdown that would dispatch it (T02 probe logs
// confirm no GameUI::Shutdown line in either run). The notification that
// does fire while vgui2.dll is still fully alive is a ClientVGUI Shutdown -
// it is the proxy's very first step (upstream BaseUI.cpp:158-176), before
// the IGameUI proxies come down and before the original Shutdown unloads
// GameUI.dll/vgui2.dll. The IPanel hooks must be removed here: leaving them
// installed past the vgui2 unload turns the engine's own teardown into a
// call through an unmapped vtable (observed as an 0xC0000005 exit).
class NativeClientVGUICallbacks final : public IVGUI2Extension_ClientVGUICallbacks {
public:
	int GetAltitude() const override { return 0; }
	void Initialize(CreateInterfaceFn* factories, int count) override {
		(void)factories;
		(void)count;
	}
	void Shutdown() override {
		// vgui2 alive; unhook the IPanel gate and disarm. The strict
		// takeover rule holds: this runs only on the engine teardown path
		// (ClientVGUI is created at BaseUI::Start and destroyed here), not
		// on map changes, so suppression never has a gap while the original
		// UI could still paint.
		rmlui::uninstallPaintGate();
		// T14: the GameUI instance is still mapped here (GameUI.dll unloads
		// in the original CBaseUI::Shutdown, after this notification) - the
		// password-prompt slot hook comes down with the gate.
		uninstallGameUiLoadingHook();
	}
	void Start() override {}
	void SetParent(vgui::VPANEL parent) override { (void)parent; }
	void UseVGUI1(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
	void HideScoreBoard(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
	void HideAllVGUIMenu(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
	void ActivateClientUI(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
	void HideClientUI(VGUI2Extension_CallbackContext* CallbackContext) override { (void)CallbackContext; }
};

NativeClientVGUICallbacks ClientVGUICallbacksInstance;

// T14: the server-password prompt rides IGameUI slot +0x58 (byte offset from
// the vtable start; CGameUI vtable 0x10080530 + 0x58 -> 0x10006c60,
// ShowServerPasswordDialog) - a Sven-added method BEYOND the 19 slots the
// VGUI2Extension proxies, so it gets its own VFTHook on the same instance.
// The replacement owns the prompt: the hook copies the borrowed server name
// into the loading feed and does NOT call the original (the vanilla
// ServerPasswordDialog must never be created - it is not reliably
// paint-gated because it is created outside the proxied callback set).
IGameUI* PasswordPromptGameUi = nullptr;
hook_t* PasswordPromptHook = nullptr;
// Engine-truthful view: ShowServerPasswordDialog(void* this, const char*
// serverName, int flag) - FUN_10006c60 forwards to the dialog with
// (name, flag): flag == 0 selects "#GameUI_ServerPasswordInvalid", nonzero
// "#GameUI_ServerRequiresPassword" (FUN_1000bc20). Slot index 22 =
// byte offset 0x58 (methods are 1-based; slot 0 is the destroying dtor).
using ShowServerPasswordFn = void(__fastcall*)(void*, int, const char*, int);

void __fastcall Hook_ShowServerPasswordDialog(void* pthis, int dummy, const char* serverName, int flag) {
	(void)pthis;
	(void)dummy;
	services::LoadingFeedEvent event;
	event.kind = services::LoadingFeedKind::kPasswordPrompt;
	event.text = serverName ? serverName : "";
	event.error = flag == 0;  // 0 = the previous password was rejected
	services::loadingFeed().push(event);
	// No original call: the vanilla dialog is out of the takeover's scope and
	// its creation is not provably gated (see the note above).
}

// Installed from the GameUI::Start notification (GameUI.dll mapped, vtable
// live; the VGUI2Extension proxy already owns slots 1..19 - index 22 does
// not overlap). Fail-closed: a slot that does not point into the GameUI.dll
// image means the layout drifted - skip with a warning (no guessed address,
// no crash); the attempt stays cancellable through the loading page.
void installPasswordPromptHook() {
	if (PasswordPromptHook)
		return;
	const HMODULE gameUi = GetModuleHandleA("GameUI.dll");
	if (!gameUi) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: GameUI.dll not loaded\n");
		return;
	}
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameUi);
	const auto nt = dos->e_magic == IMAGE_DOS_SIGNATURE
		? reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(gameUi) + dos->e_lfanew)
		: nullptr;
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: GameUI.dll image header invalid\n");
		return;
	}
	const auto imageBase = reinterpret_cast<const BYTE*>(gameUi);
	const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
	const auto factory = Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(gameUi));
	if (!factory) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: GameUI.dll has no factory\n");
		return;
	}
	auto* gameUiInstance = static_cast<IGameUI*>(factory(GAMEUI_INTERFACE_VERSION, nullptr));
	if (!gameUiInstance) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: \"%s\" unavailable\n", GAMEUI_INTERFACE_VERSION);
		return;
	}
	if (!g_pMetaHookAPI) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: metahook API unavailable\n");
		return;
	}
	void** vtable = *reinterpret_cast<void***>(gameUiInstance);
	const void* slot22 = vtable[22];
	if (slot22 < imageBase || slot22 >= imageEnd) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: IGameUI slot 22 (%p) outside the GameUI.dll image; layout drifted, skipping\n",
			slot22);
		return;
	}
	void* original = nullptr;
	hook_t* hook = g_pMetaHookAPI->VFTHook(gameUiInstance, 0, 22,
		reinterpret_cast<void*>(&Hook_ShowServerPasswordDialog), &original);
	if (!hook) {
		abc::ConsoleWarn("[AbcUi] password prompt hook: VFTHook slot 22 failed\n");
		return;
	}
	PasswordPromptGameUi = gameUiInstance;
	PasswordPromptHook = hook;
	abc::ConsolePrint("[AbcUi] password prompt hook installed (IGameUI slot 22 -> 0x%p)\n", slot22);
}

} // namespace

void init() {
	const auto module = GetModuleHandleA("VGUI2Extension.dll");
	if (!module)
		return;
	const auto factory = Sys_GetFactory((HINTERFACEMODULE)module);
	if (!factory) {
		abc::SysError("[AbcUi] Could not get factory from VGUI2Extension");
		return;
	}
	Extension = (decltype(Extension))factory(VGUI2_EXTENSION_INTERFACE_VERSION, NULL);
	if (!Extension) {
		abc::SysError("[AbcUi] Could not get interface \"%s\" from VGUI2Extension", VGUI2_EXTENSION_INTERFACE_VERSION);
		return;
	}
	Surface2 = (decltype(Surface2))factory(VGUI_SURFACE2_INTERFACE_VERSION, NULL);
	if (!Surface2) {
		abc::SysError("[AbcUi] Could not get interface \"%s\" from VGUI2Extension", VGUI_SURFACE2_INTERFACE_VERSION);
		return;
	}
	// The key-name table lives in the engine (old VGUI2 BaseUI binding).
	GameUIFuncs = (decltype(GameUIFuncs))g_pMetaHookAPI->GetEngineFactory()(VENGINE_GAMEUIFUNCS_VERSION, NULL);

	// R-4 batch C upgrade: WndProc callbacks drive the Rml mouse (clicks and
	// motion) - the async-key poll cannot see injected or message-level input.
	if (!UICallbacksRegistered) {
		Extension->RegisterBaseUICallbacks(&UICallbacksInstance);
		UICallbacksRegistered = true;
	}
	// T08: the native-UI takeover callbacks are engine-session scoped (see
	// the NativeExtension note above). Registration happens here, before
	// BaseUI::Initialize (T02 timeline: LoadClient -> registration ->
	// Initialize), so the cold-start GameUI activation already funnels
	// through them.
	if (!NativeCallbacksRegistered) {
		Extension->RegisterGameUICallbacks(&GameUICallbacksInstance);
		Extension->RegisterGameConsoleCallbacks(&GameConsoleCallbacksInstance);
		// The teardown seam: ClientVGUI Shutdown fires first inside the
		// IBaseUI::Shutdown proxy, while vgui2 is still mapped.
		Extension->RegisterClientVGUICallbacks(&ClientVGUICallbacksInstance);
		NativeExtension = Extension;
		NativeCallbacksRegistered = true;
	}
}

void shutdown() {
	// HUD-cycle scoped: this runs at every HUD_Shutdown (map change) and is
	// re-armed by the next init(). The T08 native callbacks and the IPanel
	// gate deliberately survive these cycles - the takeover has no gaps.
	if (Extension && UICallbacksRegistered) {
		Extension->UnregisterBaseUICallbacks(&UICallbacksInstance);
		UICallbacksRegistered = false;
	}
	GameUIFuncs = nullptr;
	Surface2 = nullptr;
	Extension = nullptr;
}

void shutdownNative() {
	// Plugin teardown only (plugins.cpp OnShutdown). Fallback gate removal
	// for paths that never reached the GameUI::Shutdown notification.
	rmlui::uninstallPaintGate();
	if (NativeExtension && NativeCallbacksRegistered) {
		NativeExtension->UnregisterGameUICallbacks(&GameUICallbacksInstance);
		NativeExtension->UnregisterGameConsoleCallbacks(&GameConsoleCallbacksInstance);
		NativeExtension->UnregisterClientVGUICallbacks(&ClientVGUICallbacksInstance);
		NativeCallbacksRegistered = false;
	}
	NativeExtension = nullptr;
	services::nativeUiBridge().setMenuSink(nullptr);
}

bool uiCallbacksRegistered() {
	return UICallbacksRegistered;
}

bool nativeCallbacksRegistered() {
	return NativeCallbacksRegistered;
}

bool gameUiLoadingHookInstalled() {
	return PasswordPromptHook != nullptr;
}

void uninstallGameUiLoadingHook() {
	if (PasswordPromptHook) {
		if (g_pMetaHookAPI)
			g_pMetaHookAPI->UnHook(PasswordPromptHook);
		PasswordPromptHook = nullptr;
	}
	PasswordPromptGameUi = nullptr;
}

bool available() {
	return Surface2 != nullptr;
}

int createTexture() {
	if (!Surface2)
		return warnUnavailableOnce(), 0;
	return Surface2->CreateNewTextureID(true);
}

void uploadTexture(int textureId, const unsigned char* rgba, int width, int height) {
	if (!Surface2 || textureId == 0)
		return;
	// hardwareFilter=1: linear filtering (UI glyphs/images), forceReload=false.
	Surface2->DrawSetTextureRGBA(textureId, rgba, width, height, 1, false);
}

void deleteTextureById(int textureId) {
	if (!Surface2 || textureId == 0)
		return;
	Surface2->DeleteTextureByID(textureId);
}

bool getCursorPos(int& x, int& y) {
	if (!Surface2)
		return warnUnavailableOnce(), false;
	Surface2->SurfaceGetCursorPos(x, y);
	return true;
}

bool isCursorVisible() {
	if (!Surface2)
		return false;
	return Surface2->IsCursorVisible();
}

const char* currentLanguage() {
	if (!Extension)
		return "";
	return Extension->GetCurrentLanguage();
}

const char* keyNameForKey(int keynum) {
	if (!GameUIFuncs)
		return "";
	return GameUIFuncs->Key_NameForKey(keynum);
}

const char* keyBindingForKey(int keynum) {
	if (!GameUIFuncs)
		return "";
	return GameUIFuncs->Key_BindingForKey(keynum);
}

int keyStringToKeyNum(const char* name) {
	if (!GameUIFuncs || !name)
		return -1;
	return GameUIFuncs->Key_KeyStringToKeyNum(name);
}

bool isKeyDown(const char* name) {
	if (!GameUIFuncs || !name)
		return false;
	bool down = false;
	// IGameUIFuncs slot 1 (hw.dll 0x01dbd370, T12 vtable pin): returns
	// success and writes the state; only a true+true pair counts as down.
	if (!GameUIFuncs->IsKeyDown(name, down))
		return false;
	return down;
}

std::string localizeToken(const char* token) {
	if (!token || !*token)
		return {};
	// vgui2.dll's localize implementation (VGUI_Localize003 - GameUI.dll
	// resolves its own "#GameUI_..." strings through it). Resolved lazily and
	// cached: the interface instance is process-scoped like the surface.
	static vgui::ILocalize* Localize = nullptr;
	static bool Resolved = false;
	if (!Resolved) {
		Resolved = true;
		const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
		if (vgui2) {
			const auto factory = Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(vgui2));
			if (factory)
				Localize = static_cast<vgui::ILocalize*>(factory(VGUI_LOCALIZE_INTERFACE_VERSION, nullptr));
		}
	}
	if (!Localize)
		return {};
	const wchar_t* wide = Localize->Find(token);
	if (!wide || !*wide)
		return {};
	// UTF-16 -> UTF-8 (the engine localize stores UTF-16); bounded by the
	// interface's own conversion so the codepage semantics match the shipped
	// GameUI's display path.
	char buffer[1024] = {};
	if (Localize->ConvertUnicodeToANSI(wide, buffer, sizeof(buffer)) <= 0)
		return {};
	return buffer;
}

// The engine's vmode_s is 3 x int32 {width, height, bpp} (hw.dll enumeration
// array stride 0xc, pinned in the T12 execution record); the MetaHook header
// only forward-declares the type, so the view is local.
struct EngineVideoMode {
	int Width;
	int Height;
	int Bpp;
};

// Engine-truthful mirror of the IGameUIFuncs vtable (T12 ABI trap): the
// MetaHook header declares GetCurrentRenderer with THREE outputs, but the
// shipped engine implementation (hw.dll 0x01dbddb0, reached through the
// vtable slot at 0x01dbd310) reads SIX argument slots - (name, namelen,
// windowed, hdmodels, addons, vid_level), each NULL-safe - and the shipped
// GameUI calls it with six arguments (GameUI.dll FUN_1001cca0 calls slot
// +0x1c with 6 args; the adjacent +0x18 slot with 3 for
// GetCurrentVideoMode). Calling through the 3-arg declaration leaves the
// last three stack slots undefined; the callee then writes through the
// garbage "pointers" (live-caught: engine assert dump with the stack inside
// FUN_01dbddb0 on the first settings-page open).
//
// The mirror keeps the declaration order (and therefore every vtable slot)
// of IGameUIFuncs - the base-interface destructor stays the first entry,
// which is what puts Key_NameForKey at index 2 / GetCurrentRenderer at
// index 7, the same indices T11 pinned. Only this one signature differs.
class EngineGameUIFuncs {
public:
	virtual ~EngineGameUIFuncs() = default;
	virtual bool IsKeyDown(const char* keyname, bool& isdown) = 0;
	virtual const char* Key_NameForKey(int keynum) = 0;
	virtual const char* Key_BindingForKey(int keynum) = 0;
	virtual int GetVGUI2KeyCodeForBind(const char* bind) = 0;
	virtual void GetVideoModes(void** liststart, int* count) = 0;
	virtual void GetCurrentVideoMode(int* wide, int* tall, int* bpp) = 0;
	virtual void GetCurrentRenderer(char* name, int namelen, int* windowed, int* hdmodels,
		int* addons, int* vidLevel) = 0;
	virtual bool IsConnectedToVACSecureServer() = 0;
	virtual int Key_KeyStringToKeyNum(const char* string) = 0;
};

bool currentVideoMode(int& width, int& height, int& bpp, bool& windowed) {
	if (!GameUIFuncs)
		return false;
	int w = 0, h = 0, depth = 0;
	GameUIFuncs->GetCurrentVideoMode(&w, &h, &depth); // 3 outputs: header matches
	if (w <= 0 || h <= 0)
		return false;
	char renderer[64] = {};
	int windowedFlag = 0;
	// The three extra outputs (hdmodels/addons/vid_level) are out of the T12
	// surface but MUST be passed: they are real argument slots of the engine
	// function, not optional. Scratch locals, values unused here.
	int hdmodels = 0, addons = 0, vidLevel = 0;
	auto* engine = reinterpret_cast<EngineGameUIFuncs*>(GameUIFuncs);
	engine->GetCurrentRenderer(renderer, static_cast<int>(sizeof(renderer)), &windowedFlag, &hdmodels,
		&addons, &vidLevel);
	width = w;
	height = h;
	bpp = depth;
	windowed = windowedFlag != 0;
	return true;
}

int videoModeList(video::Mode* out, int max) {
	if (!GameUIFuncs || !out || max <= 0)
		return 0;
	EngineVideoMode* list = nullptr;
	int count = 0;
	GameUIFuncs->GetVideoModes(reinterpret_cast<struct vmode_s**>(&list), &count);
	if (!list || count <= 0)
		return 0;
	if (count > max)
		count = max;
	for (int i = 0; i < count; ++i) {
		out[i].Width = list[i].Width;
		out[i].Height = list[i].Height;
		out[i].Bpp = list[i].Bpp;
	}
	return count;
}

bool centerAndUnlockCursor() {
	if (!Surface2)
		return false;
	const int x = gEngfuncs.GetWindowCenterX();
	const int y = gEngfuncs.GetWindowCenterY();
	Surface2->UnlockCursor();
	Surface2->SetCursor(vgui::dc_arrow);
	Surface2->SurfaceSetCursorPos(x, y);
	Surface2->LockCursor();
	return true;
}

void lockCursor() {
	if (!Surface2)
		return;
	// Return control to VGUI, including an already-open GameUI/console.
	Surface2->UnlockCursor();
	Surface2->SetCursor(vgui::dc_none);
	Surface2->CalculateMouseVisible();
}

bool hideGameUi() {
	// The engine IBaseUI singleton, resolved from the same factory the
	// extension's BaseUI_Install used - so the vtable this pointer carries is
	// the hooked (proxied) one, and the call keeps the outer CBaseUI engine
	// root side effects while our inner takeover answers the queries.
	static IBaseUI* BaseUi = nullptr;
	if (!BaseUi) {
		const auto factory = g_pMetaHookAPI->GetEngineFactory();
		BaseUi = factory ? (IBaseUI*)factory(BASEUI_INTERFACE_VERSION, NULL) : nullptr;
	}
	if (!BaseUi) {
		abc::ConsoleWarn("[AbcUi] IBaseUI unavailable - resume cannot run the engine-side hide\n");
		return false;
	}
	BaseUi->HideGameUI();
	return true;
}

}
