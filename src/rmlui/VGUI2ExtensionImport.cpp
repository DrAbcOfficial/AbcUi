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
		// Lifecycle observation (T14 consumes the values through the typed
		// seam later); the vanilla loading dialog never opens because the
		// paint gate is armed.
		(void)resourceType;
		(void)resourceName;
		if (!CallbackContext->IsPost)
			services::nativeUiBridge().noteLoadingStart();
	}
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)resourceName;
		if (CallbackContext->IsPost)
			return;
		auto& bridge = services::nativeUiBridge();
		bridge.noteLoadingFinish();
		// A finished level load is the engine-confirmed "in game" fact the
		// vanilla connected-only hide gate reads.
		if (resourceType && !_stricmp(resourceType, "level"))
			bridge.noteLevelLoaded();
	}
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)progressType;
		(void)progressSteps;
		(void)CallbackContext;
	}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)progressPoint;
		(void)progressFraction;
		(void)CallbackContext;
	}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)bError;
		(void)failureReason;
		(void)extendedReason;
		(void)CallbackContext;
	}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)statusText;
		(void)CallbackContext;
	}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)progress;
		(void)CallbackContext;
	}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {
		(void)statusText;
		(void)CallbackContext;
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
