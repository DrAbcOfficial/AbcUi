// vgui2ext implementation - see VGUI2ExtensionImport.h.
#include <metahook.h>

#include "VGUI2ExtensionImport.h"

#include <ISurface2.h>
#include <IVGUI2Extension.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

namespace vgui2ext {

namespace {
IVGUI2Extension* Extension = nullptr;
vgui::ISurface2* Surface2 = nullptr;
bool WarnedUnavailable = false;

bool warnUnavailableOnce() {
	if (!WarnedUnavailable) {
		abc::ConsoleWarn("[AbcUi] VGUI2Extension.dll not found - surface textures and mouse polling disabled\n");
		WarnedUnavailable = true;
	}
	return false;
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
}

void shutdown() {
	Surface2 = nullptr;
	Extension = nullptr;
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

}
