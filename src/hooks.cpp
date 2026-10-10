// AbcUi hook spine (see hooks.h for the consumption map).
#include <metahook.h>

#include "hooks.h"

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "data/HudState.h"
#include "data/NetworkMessages.h"
#include "data/TextFeed.h"
#include "data/WeaponResource.h"
#include "framework/PanelManager.h"
#include "rmlui/InputBridge.h"
#include "rmlui/RmlUiModule.h"

namespace hooks {

namespace {

// Vanilla HUD suppression (from: ABCEnchance exportfuncs.cpp HUDList sig +
// the HUD_Redraw flag clear). The SC client keeps its built-in HUD panels in
// a list headed by a client-dll variable; the old mod located that variable
// through the mov-ecx-imm32 prologue byte of the client's HUD_VidInit and
// cleared every panel's visible bit per HUD_Redraw so the Rml HUD stands
// alone. VanilliaHud layout: {vtable, x, y, type, flags} - visible bit =
// flags bit0.
constexpr size_t kVanillaHudFlagsOffset = 16;
constexpr size_t kVanillaHudMaxPanels = 5;

DWORD VanillaHudListAddr = 0;
DWORD VanillaHudPanels[kVanillaHudMaxPanels] = {};
size_t VanillaHudPanelCount = 0;

void findVanillaHudList() {
	// g_pMetaSave->pExportFuncs = the pristine client export table (pre-patch).
	auto* clientVidInit = reinterpret_cast<void*>(g_pMetaSave->pExportFuncs->HUD_VidInit);
	if (!clientVidInit)
		return;
	const auto addr = static_cast<unsigned char*>(
		g_pMetaHookAPI->SearchPattern(clientVidInit, 0x10, "\xB9", 1));
	if (!addr)
		abc::SysError("[AbcUi] Could not found: HUDList");
	VanillaHudListAddr = *(DWORD*)(addr + 1);
}

void rebuildVanillaHudPanels() {
	VanillaHudPanelCount = 0;
	if (!VanillaHudListAddr)
		return;
	struct HudListNode {
		DWORD panel;
		DWORD next;
	};
	HudListNode* node = reinterpret_cast<HudListNode*>(*(DWORD*)VanillaHudListAddr);
	for (size_t i = 0; i < kVanillaHudMaxPanels && node; i++) {
		if (node->panel)
			VanillaHudPanels[VanillaHudPanelCount++] = node->panel;
		node = reinterpret_cast<HudListNode*>(node->next);
	}
}

void hideVanillaHudPanels() {
	for (size_t i = 0; i < VanillaHudPanelCount; i++)
		(*(DWORD*)(VanillaHudPanels[i] + kVanillaHudFlagsOffset)) &= ~1;
}

} // namespace

static void H_HUD_Init() {
	gExportfuncs.HUD_Init();
	// Command/cvar registration is HUD_Init-time only (plan 08 §4.9);
	// guarded like AbcEssential in case the engine re-runs HUD_Init.
	static bool CommandsRegistered = false;
	if (!CommandsRegistered) {
		CommandsRegistered = true;
		netmsg::init();
		hudstate::init();
		weaponres::init();
		textfeed::init();
		framework::PanelManager::registerPanelCvars();
		rmlui::registerCommands();
	}
}

static int H_HUD_VidInit() {
	rebuildVanillaHudPanels();
	int result = gExportfuncs.HUD_VidInit();
	weaponres::reset();
	rmlui::onVidInit();
	return result;
}

static void H_HUD_Frame(double frametime) {
	gExportfuncs.HUD_Frame(frametime);
	rmlui::onFrame(frametime);
}

static int H_HUD_Redraw(float time, int intermission) {
	// Old order: strip the vanilla panels' visible bit before the client
	// redraws, then chain.
	hideVanillaHudPanels();
	return gExportfuncs.HUD_Redraw(time, intermission);
}

static int H_HUD_UpdateClientData(struct client_data_s* cdata, float time) {
	// Observe-first (the old exportfuncs order): data layer before the
	// engine's own handler.
	hudstate::onUpdateClientData(cdata);
	return gExportfuncs.HUD_UpdateClientData(cdata, time);
}

static void H_HUD_TxferPredictionData(struct entity_state_s* ps, const struct entity_state_s* pps,
	struct clientdata_s* pcd, const struct clientdata_s* ppcd,
	struct weapon_data_s* wd, const struct weapon_data_s* pwd) {
	weaponres::onTxferPredictionData(pwd);
	gExportfuncs.HUD_TxferPredictionData(ps, pps, pcd, ppcd, wd, pwd);
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
	pExportFuncs->HUD_Redraw = H_HUD_Redraw;
	pExportFuncs->HUD_Frame = H_HUD_Frame;
	pExportFuncs->HUD_Key_Event = H_HUD_Key_Event;
	pExportFuncs->HUD_UpdateClientData = H_HUD_UpdateClientData;
	pExportFuncs->HUD_TxferPredictionData = H_HUD_TxferPredictionData;
	pExportFuncs->HUD_Shutdown = H_HUD_Shutdown;
}

void locateVanillaHudList() {
	findVanillaHudList();
}

}
