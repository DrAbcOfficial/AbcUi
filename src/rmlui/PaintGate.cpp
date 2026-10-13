// rmlui::PaintGate implementation - see PaintGate.h.
#include <metahook.h>

#include "PaintGate.h"

#include <IPanel.h>

#include <abc/Console.h>
#include <abc/Plugin.h>

#include "../services/GameUiBridge.h"

#include <Windows.h>

namespace rmlui {

namespace {

// Install-time-captured handles. Guarded by Installed: the two hooks, the
// IPanel pointer and the originals are one atomic bundle (T08 step 7 -
// partial installs are undone before failure is reported).
bool Installed = false;
vgui::IPanel* Panel = nullptr;
void(__fastcall* OriginalPaintTraverse)(void*, int, vgui::VPANEL, bool, bool) = nullptr;
vgui::VPANEL(__fastcall* OriginalIsWithinTraverse)(void*, int, vgui::VPANEL, int, int, bool) = nullptr;
hook_t* TraverseHook = nullptr;
hook_t* HitTestHook = nullptr;

// Clear the keyboard/mouse input flags of one native panel and its whole
// subtree. Conditional writes only: after the first pass every flag already
// reads false, so steady state is pure reads (no vgui state churn per frame).
// Works on the module-identified entry because GameUI content is exactly the
// panels the module rule names - the engine roots and the client module never
// enter here.
void clearNativeInput(vgui::VPANEL panel, uint32_t& cleared) {
	if (!Panel || !panel)
		return;
	if (Panel->IsKeyBoardInputEnabled(panel)) {
		Panel->SetKeyBoardInputEnabled(panel, false);
		++cleared;
	}
	if (Panel->IsMouseInputEnabled(panel)) {
		Panel->SetMouseInputEnabled(panel, false);
		++cleared;
	}
	const int children = Panel->GetChildCount(panel);
	for (int i = 0; i < children; i++)
		clearNativeInput(Panel->GetChild(panel, i), cleared);
}

// The selective paint filter. No IsVisible/IsPopup reads (the original's own
// decision logic stays untouched for everything allowed); the module string
// is the whole rule and must match the install-reported identities exactly
// (T02 §6: "GameUI" / "ServerBrowser"; never "GAMEUIDLL").
void __fastcall H_PaintTraverse(void* pthis, int dummy, vgui::VPANEL vguiPanel, bool forceRepaint, bool allowForce) {
	const char* module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
	if (services::nativeUiBridge().gatePaintAttempt(module)) {
		uint32_t cleared = 0;
		clearNativeInput(vguiPanel, cleared);
		if (cleared)
			services::nativeUiBridge().noteInputFlagsCleared(cleared);
		return;
	}
	if (OriginalPaintTraverse)
		OriginalPaintTraverse(pthis, dummy, vguiPanel, forceRepaint, allowForce);
}

// The mouse hit-test filter: a native module panel reports "nothing here".
vgui::VPANEL __fastcall H_IsWithinTraverse(void* pthis, int dummy, vgui::VPANEL vguiPanel, int x, int y, bool traversePopups) {
	const char* module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
	if (services::nativeUiBridge().gateHitTestAttempt(module))
		return 0;
	if (OriginalIsWithinTraverse)
		return OriginalIsWithinTraverse(pthis, dummy, vguiPanel, x, y, traversePopups);
	return 0;
}

// Anything that can be undone without the hook objects (pre-hook failures).
void failCleanup() {
	Panel = nullptr;
	OriginalPaintTraverse = nullptr;
	OriginalIsWithinTraverse = nullptr;
}

} // namespace

void installPaintGate() {
	if (Installed)
		return;

	// Step 1: the vgui2 module and its image range. The slot identity checks
	// below need both; a missing module is fatal (the strict seam cannot be
	// established without it).
	const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
	if (!vgui2)
		abc::SysError("[AbcUi] paint gate: vgui2.dll not loaded; cannot establish native UI suppression");
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(vgui2);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		abc::SysError("[AbcUi] paint gate: vgui2.dll image header invalid");
	const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(vgui2) + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		abc::SysError("[AbcUi] paint gate: vgui2.dll NT header invalid");
	const auto imageBase = reinterpret_cast<const BYTE*>(vgui2);
	const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;

	// Step 2: the IPanel interface through the vgui2 factory.
	if (!g_pMetaHookAPI)
		abc::SysError("[AbcUi] paint gate: metahook API unavailable");
	CreateInterfaceFn getFactory = Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(vgui2));
	if (!getFactory)
		abc::SysError("[AbcUi] paint gate: vgui2.dll has no factory");
	auto* panel = static_cast<vgui::IPanel*>(getFactory(VGUI_PANEL_INTERFACE_VERSION, nullptr));
	if (!panel) {
		failCleanup();
		abc::SysError("[AbcUi] paint gate: \"%s\" unavailable from vgui2.dll", VGUI_PANEL_INTERFACE_VERSION);
	}

	// Step 3: slot identity (hook-contract §8): both slots must point into
	// the vgui2 image. A foreign pointer means the layout drifted - fail
	// closed instead of hooking something unknown. Slot numbers follow the
	// IPanel declaration order (methods are 1-based; the destroying
	// destructor holds slot 0): PaintTraverse = 41, IsWithinTraverse = 43 -
	// the same numbering the T02 probe proved for slot 41.
	void** vtable = *reinterpret_cast<void***>(panel);
	const void* slot41 = vtable[41];
	const void* slot43 = vtable[43];
	if (slot41 < imageBase || slot41 >= imageEnd) {
		failCleanup();
		abc::SysError("[AbcUi] paint gate: IPanel slot 41 (%p) outside the vgui2 image [%p..%p); layout drifted",
			slot41, imageBase, imageEnd);
	}
	if (slot43 < imageBase || slot43 >= imageEnd) {
		failCleanup();
		abc::SysError("[AbcUi] paint gate: IPanel slot 43 (%p) outside the vgui2 image; layout drifted",
			slot43, imageBase, imageEnd);
	}

	// Step 4: install both hooks. The bridge stays unarmed until this
	// succeeded, so a failed install cannot have suppressed anything yet.
	void* originalTraverse = nullptr;
	hook_t* traverseHook = g_pMetaHookAPI->VFTHook(panel, 0, 41, reinterpret_cast<void*>(&H_PaintTraverse), &originalTraverse);
	void* originalHitTest = nullptr;
	hook_t* hitTestHook = traverseHook
		? g_pMetaHookAPI->VFTHook(panel, 0, 43, reinterpret_cast<void*>(&H_IsWithinTraverse), &originalHitTest)
		: nullptr;
	if (!traverseHook || !originalTraverse || !hitTestHook || !originalHitTest) {
		if (traverseHook)
			g_pMetaHookAPI->UnHook(traverseHook);
		if (hitTestHook)
			g_pMetaHookAPI->UnHook(hitTestHook);
		failCleanup();
		abc::SysError("[AbcUi] paint gate: VFTHook of IPanel slots 41/43 failed");
	}

	Panel = panel;
	OriginalPaintTraverse = reinterpret_cast<decltype(OriginalPaintTraverse)>(originalTraverse);
	OriginalIsWithinTraverse = reinterpret_cast<decltype(OriginalIsWithinTraverse)>(originalHitTest);
	TraverseHook = traverseHook;
	HitTestHook = hitTestHook;
	Installed = true;
	services::nativeUiBridge().arm();
	abc::ConsolePrint("[AbcUi] paint gate: installed (IPanel slots 41/43; native UI suppression armed)\n");
}

void uninstallPaintGate() {
	// The bridge disarms only here: the strict takeover keeps suppressing
	// until the original UI can no longer paint (ClientVGUI::Shutdown is
	// the primary seam; GameUI::Shutdown/teardown are fallbacks, §9). Everything below
	// runs on the main thread inside that callback.
	services::nativeUiBridge().disarm();
	if (!Installed)
		return;
	// UnHook only reports "found" - at teardown MetaHook may already have
	// freed the hook records (MH_FreeAllHook restores the same vtable
	// entries), and the pointer comparison inside UnHook is safe for that.
	if (TraverseHook) {
		g_pMetaHookAPI->UnHook(TraverseHook);
		TraverseHook = nullptr;
	}
	if (HitTestHook) {
		g_pMetaHookAPI->UnHook(HitTestHook);
		HitTestHook = nullptr;
	}
	Installed = false;
	Panel = nullptr;
	OriginalPaintTraverse = nullptr;
	OriginalIsWithinTraverse = nullptr;
	abc::ConsolePrint("[AbcUi] paint gate: uninstalled\n");
}

bool paintGateInstalled() {
	return Installed;
}

}
