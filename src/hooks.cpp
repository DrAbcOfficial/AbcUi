// AbcUi hook spine (see hooks.h for the consumption map).
#include <metahook.h>

#include "hooks.h"

#include <abc/Plugin.h>

#include "rmlui/InputBridge.h"
#include "rmlui/RmlUiModule.h"

namespace hooks {

static void H_HUD_Init() {
	gExportfuncs.HUD_Init();
	// Command/cvar registration is HUD_Init-time only (plan 08 §4.9).
	rmlui::registerCommands();
}

static int H_HUD_VidInit() {
	int result = gExportfuncs.HUD_VidInit();
	rmlui::onVidInit();
	return result;
}

static void H_HUD_Frame(double frametime) {
	gExportfuncs.HUD_Frame(frametime);
	rmlui::onFrame(frametime);
}

static int H_HUD_Key_Event(int down, int keynum, const char* pszCurrentBinding) {
	// Observe-first: feed Rml, then always let the engine/binding chain run.
	// Key consumption (return-1 semantics) is a batch-A/C concern
	// (plan 05 §3.5 input routing rules).
	rmlui::onKeyEvent(down, keynum);
	return gExportfuncs.HUD_Key_Event(down, keynum, pszCurrentBinding);
}

static void H_HUD_Shutdown() {
	rmlui::onHudShutdown();
	gExportfuncs.HUD_Shutdown();
}

void attachExports(cl_exportfuncs_t* pExportFuncs) {
	pExportFuncs->HUD_Init = H_HUD_Init;
	pExportFuncs->HUD_VidInit = H_HUD_VidInit;
	pExportFuncs->HUD_Frame = H_HUD_Frame;
	pExportFuncs->HUD_Key_Event = H_HUD_Key_Event;
	pExportFuncs->HUD_Shutdown = H_HUD_Shutdown;
}

}
