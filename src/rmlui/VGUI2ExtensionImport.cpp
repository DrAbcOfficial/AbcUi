// vgui2ext implementation - see VGUI2ExtensionImport.h.
#include <metahook.h>

#include "VGUI2ExtensionImport.h"

#include <ISurface2.h>
#include <vgui/Cursor.h>
#include <IVGUI2Extension.h>
#include <IGameUIFuncs.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "RmlUiModule.h"

namespace vgui2ext {

namespace {

IVGUI2Extension* Extension = nullptr;
vgui::ISurface2* Surface2 = nullptr;
IGameUIFuncs* GameUIFuncs = nullptr;
bool WarnedUnavailable = false;
bool UICallbacksRegistered = false;

bool warnUnavailableOnce() {
	if (!WarnedUnavailable) {
		abc::ConsoleWarn("[AbcUi] VGUI2Extension.dll not found - surface textures and mouse polling disabled\n");
		WarnedUnavailable = true;
	}
	return false;
}

// R-4 callback path (batch C): the engine surface WndProc sees every UI-level
// mouse message - including injected ones the async-key poll cannot observe
// (GetAsyncKeyState never sees SDL/window-message input). Observation-only:
// the result stays IGNORED, the vanilla surface still handles everything.
class UICallbacks final : public IVGUI2Extension_BaseUICallbacks {
public:
	int GetAltitude() const override { return 0; }
	void Initialize(CreateInterfaceFn* factories, int count) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion) override {}
	void Shutdown() override {}
	void Key_Event(int& down, int& keynum, const char*& pszCurrentBinding,
		VGUI2Extension_CallbackContext* CallbackContext) override {}
	void CallEngineSurfaceAppProc(void*& pevent, void*& userData,
		VGUI2Extension_CallbackContext* CallbackContext) override {}
	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam,
		VGUI2Extension_CallbackContext* CallbackContext) override {
		rmlui::onSurfaceWndProc(msg, wparam, lparam);
	}
	void Paint(int& x, int& y, int& right, int& bottom,
		VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ShowConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
};

UICallbacks UICallbacksInstance;

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
}

void shutdown() {
	if (Extension && UICallbacksRegistered) {
		Extension->UnregisterBaseUICallbacks(&UICallbacksInstance);
		UICallbacksRegistered = false;
	}
	GameUIFuncs = nullptr;
	Surface2 = nullptr;
	Extension = nullptr;
}

bool uiCallbacksRegistered() {
	return UICallbacksRegistered;
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

}
