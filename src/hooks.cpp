// AbcUi hook spine (see hooks.h for the consumption map).
#include <metahook.h>

#include "hooks.h"

#include <abc/Console.h>
#include <abc/Plugin.h>

#include <keydefs.h>

#include "data/HudState.h"
#include "data/NetworkMessages.h"
#include "data/PlayerResource.h"
#include "data/TextFeed.h"
#include "data/WeaponResource.h"
#include "framework/PanelManager.h"
#include "panels/gindicator/GIndicatorPanel.h"
#include "panels/motd/MotdPanel.h"
#include "panels/scoreboard/ScoreboardPanel.h"
#include "panels/vote/VotePanel.h"
#include "rmlui/InputBridge.h"
#include "rmlui/RmlUiModule.h"
#include "rmlui/SteamAPI.h"

namespace hooks {

engine_studio_api_s* EngineStudioApi = nullptr;
const hudpanel_info_s* NativeHudPanelInfo = nullptr;
GetClientColorFn GetClientColor = nullptr;

namespace {

// Vanilla HUD suppression (from: ABCEnchance exportfuncs.cpp HUDList sig +
// the HUD_Redraw flag clear). The SC client keeps its built-in HUD panels in
// a list headed by a client-dll variable; the old mod located that variable
// through the mov-ecx-imm32 prologue byte of the client's HUD_VidInit and
// cleared every panel's visible bit per HUD_Redraw so the Rml HUD stands
// alone. VanilliaHud layout: {vtable, x, y, type, flags} - visible bit =
// flags bit0.
//
// Index 3 is SKIPPED on purpose: every historical variant of the old code
// left exactly one list slot visible (the dynamic_cast era commented out its
// clear; the index era's switch is 0,1,2,4). Which SC panel that slot is was
// not fully pinned down (candidates: CHudAmmoSecondary / CHudMenu); the port
// reproduces the old code's observable behavior.
constexpr size_t kVanillaHudFlagsOffset = 16;
constexpr size_t kVanillaHudMaxPanels = 5;
constexpr size_t kVanillaHudSkipIndex = 3; // deliberately NOT suppressed (old code parity)

DWORD VanillaHudListAddr = 0;
DWORD VanillaHudPanels[kVanillaHudMaxPanels] = {};
size_t VanillaHudPanelCount = 0;

void findVanillaHudList() {
	// g_pMetaSave->pExportFuncs = the pristine client export table (pre-patch).
	auto* clientVidInit = reinterpret_cast<void*>(g_pMetaSave->pExportFuncs->HUD_VidInit);
	if (!clientVidInit)
		return;
	// The pattern is deliberately minimal (the mov-ecx-imm32 prologue byte
	// within the first 0x10 bytes of HUD_VidInit - a wider signature would
	// need a per-build disassembly), so the extracted pointer is range-
	// checked against the mirrored client image before anything reads it.
	const auto addr = static_cast<unsigned char*>(
		g_pMetaHookAPI->SearchPattern(clientVidInit, 0x10, "\xB9", 1));
	if (!addr)
		abc::SysError("[AbcUi] Could not found: HUDList");
	const DWORD clientBase = reinterpret_cast<DWORD>(g_pMetaHookAPI->GetClientBase());
	const DWORD clientSize = g_pMetaHookAPI->GetClientSize();
	VanillaHudListAddr = *(DWORD*)(addr + 1);
	if (VanillaHudListAddr < clientBase || VanillaHudListAddr - clientBase >= clientSize)
		abc::SysError("[AbcUi] HUDList pointer outside the client image: %08x", VanillaHudListAddr);
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
		if (node->panel && i != kVanillaHudSkipIndex)
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
		playerres::init();
		textfeed::init();
		framework::PanelManager::registerPanelCvars();
		rmlui::registerCommands();
	}
}

static int H_HUD_VidInit() {
	rebuildVanillaHudPanels();
	int result = gExportfuncs.HUD_VidInit();
	weaponres::reset();
	playerres::resetAll();
	rmlui::onVidInit();
	return result;
}

static void H_HUD_Frame(double frametime) {
	gExportfuncs.HUD_Frame(frametime);
	rmlui::onFrame(frametime);
}

static int H_HUD_Redraw(float time, int intermission) {
	// Old order: strip the vanilla panels' visible bit before the client
	// redraws, then chain. The reposition pump runs in this phase (the old
	// vgui think phase - the only phase where pTriAPI->WorldToScreen sees the
	// live view matrices). HUD_Redraw's `time` is the absolute client clock
	// (not a delta like HUD_Frame's) - convert for the frame pumps.
	static float LastRedrawTime = 0.0f;
	float delta = time - LastRedrawTime;
	LastRedrawTime = time;
	if (delta < 0.0f || delta > 0.5f)
		delta = 0.0f;
	hideVanillaHudPanels();
	// Old Viewport::SetInterMission -> ShowPanel(intermission > 0); the
	// +/-showscores commands stay dead while it lasts.
	panels::scoreboard::setIntermission(intermission != 0);
	rmlui::onRedraw(delta);
	return gExportfuncs.HUD_Redraw(time, intermission);
}

static int H_HUD_UpdateClientData(struct client_data_s* cdata, float time) {
	// Observe-first (the old exportfuncs order): data layer before the
	// engine's own handler.
	hudstate::onUpdateClientData(cdata);
	return gExportfuncs.HUD_UpdateClientData(cdata, time);
}

static void H_HUD_TxferLocalOverrides(struct entity_state_s* state, const struct clientdata_s* client) {
	// Old exportfuncs: gClientData = client (the prediction client data the
	// crosshair/effect panels read through hudstate).
	hudstate::onTxferLocalOverrides(client);
	gExportfuncs.HUD_TxferLocalOverrides(state, client);
}

static int H_HUD_AddEntity(int type, struct cl_entity_s* ent, const char* modelname) {
	// Old Viewport::AddEntity routing (batch B: grenade indicator; the item
	// highlight half lands with batch D).
	panels::gindicator::onAddEntity(ent, modelname);
	return gExportfuncs.HUD_AddEntity(type, ent, modelname);
}

static void H_HUD_TxferPredictionData(struct entity_state_s* ps, const struct entity_state_s* pps,
	struct clientdata_s* pcd, const struct clientdata_s* ppcd,
	struct weapon_data_s* wd, const struct weapon_data_s* pwd) {
	weaponres::onTxferPredictionData(pwd);
	gExportfuncs.HUD_TxferPredictionData(ps, pps, pcd, ppcd, wd, pwd);
}

namespace {

// Wheel ownership (batch C): the engine delivers wheel pulses through
// HUD_Key_Event; while a scrollable Rml surface is up (scoreboard pointer
// mode, open motd) they scroll it and never reach the weapon switch.
bool uiOwnsWheel(int down, int keynum) {
	if (!down || (keynum != K_MWHEELUP && keynum != K_MWHEELDOWN))
		return false;
	if (panels::scoreboard::wantsWheel())
		return true;
	return panels::motd::wantsWheel();
}

} // namespace

static int H_HUD_Key_Event(int down, int keynum, const char* pszCurrentBinding) {
#ifndef NDEBUG
	// Key-chain trace (P6 audit round): mouse/wheel keys are rare, print them
	// with the consumption verdict.
	if (keynum >= K_MOUSE2 || keynum == K_MWHEELUP || keynum == K_MWHEELDOWN)
		abc::ConsolePrint("[AbcUi] key: down=%d keynum=%d consumed=%d\n", down, keynum,
			(panels::vote::onKeyEvent(down, keynum) || panels::scoreboard::onKeyEvent(down, keynum)) ? 1 : 0);
#endif
	// Batch-C UI key consumers first (vote answers, scoreboard pointer
	// toggle): a hit swallows the key from the engine/binding chain (old
	// Viewport::KeyInput false-return).
	if (panels::vote::onKeyEvent(down, keynum) || panels::scoreboard::onKeyEvent(down, keynum)) {
		rmlui::onKeyEvent(down, keynum);
		return 0;
	}
	if (uiOwnsWheel(down, keynum)) {
		rmlui::onWheel(down, keynum);
		return 0;
	}
	// Observe-first: feed Rml, then let the engine/binding chain run.
	rmlui::onKeyEvent(down, keynum);
	return gExportfuncs.HUD_Key_Event(down, keynum, pszCurrentBinding);
}

static int H_HUD_GetStudioModelInterface(int version, struct r_studio_interface_s** ppinterface,
	struct engine_studio_api_s* pstudio) {
	// Old exportfuncs: mirror the engine studio API once (player info reads
	// in data::playerres; AbcEssential holds its own copy - D-01 no sharing).
	EngineStudioApi = pstudio;
	return gExportfuncs.HUD_GetStudioModelInterface(version, ppinterface, pstudio);
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
	pExportFuncs->HUD_GetStudioModelInterface = H_HUD_GetStudioModelInterface;
	pExportFuncs->HUD_UpdateClientData = H_HUD_UpdateClientData;
	pExportFuncs->HUD_TxferPredictionData = H_HUD_TxferPredictionData;
	pExportFuncs->HUD_TxferLocalOverrides = H_HUD_TxferLocalOverrides;
	pExportFuncs->HUD_AddEntity = H_HUD_AddEntity;
	pExportFuncs->HUD_Shutdown = H_HUD_Shutdown;
}

void locatePlayerColor() {
	// from: ABCEnchance exportfuncs.cpp SC_GETCLIENTCOLOR_SIG (the client's
	// per-player color table accessor the scoreboard/playerboard tint with).
	constexpr char pattern[] = "\x8B\x4C\x24\x04\x85\xC9\x2A\x2A\x6B\xC1\x5C\x0F\xBF\x80\x2A\x2A\x2A\x2A\x48\x83";
	GetClientColor = reinterpret_cast<GetClientColorFn>(g_pMetaHookAPI->SearchPattern(
		g_pMetaHookAPI->GetClientBase(), g_pMetaHookAPI->GetClientSize(), pattern, sizeof(pattern) - 1));
	if (!GetClientColor)
		abc::SysError("[AbcUi] Could not found: GetClientColor");
}

void locateNativeHudPanelInfo() {
	// from: ABCEnchance exportfuncs.cpp NativeHUDPanelInfo (SC client keeps
	// its native per-player HUD data - frags/health/armor/donor/admin - in a
	// global array; the old sig pins the mov-imm32 that stores its address).
	// Pattern deliberately byte-scoped; the extracted pointer is range-
	// checked against the mirrored client image before anything reads it.
	constexpr char pattern[] = "\xC6\x85\x8F\xFE\xFF\xFF\x01\x66\x89\x90";
	const auto addr = static_cast<unsigned char*>(g_pMetaHookAPI->SearchPattern(
		g_pMetaHookAPI->GetClientBase(), g_pMetaHookAPI->GetClientSize(), pattern, sizeof(pattern) - 1));
	if (!addr)
		abc::SysError("[AbcUi] Could not found: NativeHUDPanelInfo");
	const DWORD clientBase = reinterpret_cast<DWORD>(g_pMetaHookAPI->GetClientBase());
	const DWORD clientSize = g_pMetaHookAPI->GetClientSize();
	const DWORD tableAddr = *(DWORD*)(addr + 10) + 8;
	if (tableAddr < clientBase || tableAddr - clientBase >= clientSize)
		abc::SysError("[AbcUi] NativeHUDPanelInfo pointer outside the client image: %08x", tableAddr);
	NativeHudPanelInfo = reinterpret_cast<const hudpanel_info_s*>(tableAddr);
}

void locateVanillaHudList() {
	findVanillaHudList();
}

}
