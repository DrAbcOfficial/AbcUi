// rmlui::PaintGate implementation - see PaintGate.h.
#include <metahook.h>

#include "PaintGate.h"

#include <IPanel.h>
#include <ISurface.h>

#include <abc/Console.h>
#include <abc/GameData.h>
#include <abc/Plugin.h>

#include <abcui/events.h>

#include "../modsys/Arbitration.h"
#include "../services/GameUiBridge.h"

#include <Windows.h>
#include <cstddef>
#include <cstring>
#include <vector>

namespace rmlui {

namespace {

// Install-time-captured handles. Guarded by Installed: the two hooks, the
// IPanel pointer and the originals are one atomic bundle (T08 step 7 -
// partial installs are undone before failure is reported).
bool Installed = false;
vgui::IPanel *Panel = nullptr;
vgui::ISurface *Surface = nullptr;
void(__fastcall *OriginalPaintTraverse)(void *, int, vgui::VPANEL, bool,
                                        bool) = nullptr;
vgui::VPANEL(__fastcall *OriginalIsWithinTraverse)(void *, int, vgui::VPANEL,
                                                   int, int, bool) = nullptr;
hook_t *TraverseHook = nullptr;
hook_t *HitTestHook = nullptr;

// The vgui2 image range captured at install; the client-panel paint hook and
// the interface slots are validated against it.
const std::byte *VguiImageBase = nullptr;
const std::byte *VguiImageEnd = nullptr;

// Clear the keyboard/mouse input flags of one native panel and its whole
// subtree. Conditional writes only: after the first pass every flag already
// reads false, so steady state is pure reads (no vgui state churn per frame).
void clearNativeInput(vgui::VPANEL panel, uint32_t &cleared) {
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

// The client viewport object (TeamFortressViewport) that owns the observer
// bar. Read from its gamedata global every call: the pointer is created
// during client init and may be replaced by a client reload.
void **ViewportSlot = nullptr;

// The client viewport's observer bar, re-located from the vgui2 root while
// the replacement owns the surface. The client rebuilds the viewport (and its
// SpectatorPanel) on a client VidInit, so only the handle value and the
// client pointer are cached, and both are used for comparisons only.
vgui::VPANEL SpectatorPanelVPanel = 0;
void *SpectatorPanelClient = nullptr;
bool SpectatorSuppressionLogged = false;

// The vgui2 root captured from the interface traversal (parentless panel).
// Used only when the surface interface is unavailable; both feed the same
// walk.
vgui::VPANEL RootVPanel = 0;
int FailedWalks = 0;

using ClientPaintFn = void(__fastcall *)(void *, int, bool, bool);
ClientPaintFn OriginalClientPaint = nullptr;
hook_t *ClientPaintHook = nullptr;

bool spectatorClaimed() {
  const AbcUiNativeHudEvent event{ABCUI_NATIVE_HUD_SPECTATOR};
  return framework::arbitration::gate(ABCUI_CHANNEL_HUD_NATIVE_DRAW, &event);
}

// The observer bar paints through its own client-panel PaintTraverse: the
// vgui2 traversal calls the virtual directly for children, so the IPanel
// interface hook below never sees this subtree. The virtual is shared by
// every client panel (SpectatorPanel overrides nothing), so it is hooked once
// on the first located bar and skips only that object.
void __fastcall H_ClientPanelPaintTraverse(void *self, int dummy,
                                           bool forceRepaint,
                                           bool allowForce) {
  if (self && self == SpectatorPanelClient && spectatorClaimed()) {
    if (!SpectatorSuppressionLogged) {
      SpectatorSuppressionLogged = true;
      abc::ConsolePrint("[AbcUi] paint gate: native spectator panel "
                        "suppressed at Panel::PaintTraverse\n");
    }
    return;
  }
  if (OriginalClientPaint)
    OriginalClientPaint(self, dummy, forceRepaint, allowForce);
}

void installClientPanelPaintHook(void *clientPanel) {
  if (ClientPaintHook || !clientPanel || !VguiImageBase || !VguiImageEnd)
    return;
  void **vtable = *static_cast<void ***>(clientPanel);
  if (!vtable)
    return;
  // vgui2 publishes Panel::PaintTraverse at virtual slot 3 (gamedata
  // vfunc_index 3); SpectatorPanel inherits it unchanged.
  const auto *target = reinterpret_cast<const std::byte *>(vtable[3]);
  if (!target || target < VguiImageBase || target >= VguiImageEnd)
    return;
  ClientPaintHook = g_pMetaHookAPI->InlineHook(
      const_cast<std::byte *>(target),
      reinterpret_cast<void *>(&H_ClientPanelPaintTraverse),
      reinterpret_cast<void **>(&OriginalClientPaint));
  if (ClientPaintHook)
    abc::ConsolePrint("[AbcUi] paint gate: client panel paint hook installed "
                      "(Panel::PaintTraverse)\n");
}

int childLabelCount(vgui::VPANEL panel) {
  int labels = 0;
  const int children = Panel ? Panel->GetChildCount(panel) : 0;
  for (int i = 0; i < children; ++i) {
    const char *className = Panel->GetClassName(Panel->GetChild(panel, i));
    if (className && std::strcmp(className, "Label") == 0)
      ++labels;
  }
  return labels;
}

// Locate the observer bar from the vgui2 root. The root is the surface's
// embedded panel; the viewport is the panel whose client object is gViewPort;
// the bar is its direct child with five constructor children (two plates and
// three labels). The structural form is used only when gViewPort is absent.
bool refreshSpectatorPanel() {
  if (!Panel)
    return false;
  const vgui::VPANEL root =
      Surface ? Surface->GetEmbeddedPanel() : RootVPanel;
  if (!root)
    return false;

  std::vector<vgui::VPANEL> level{root};
  vgui::VPANEL viewport = 0;
  vgui::VPANEL structural = 0;
  for (int depth = 0; depth < 8 && !level.empty(); ++depth) {
    std::vector<vgui::VPANEL> next;
    for (const vgui::VPANEL panel : level) {
      if (!viewport && ViewportSlot && *ViewportSlot &&
          Panel->Client(panel) == *ViewportSlot)
        viewport = panel;
      if (!structural && Panel->GetChildCount(panel) == 5 &&
          childLabelCount(panel) == 3)
        structural = panel;
      const int children = Panel->GetChildCount(panel);
      for (int i = 0; i < children; ++i)
        next.push_back(Panel->GetChild(panel, i));
    }
    level.swap(next);
  }

  vgui::VPANEL spectator = 0;
  if (viewport) {
    const int children = Panel->GetChildCount(viewport);
    for (int i = 0; i < children; ++i) {
      const vgui::VPANEL child = Panel->GetChild(viewport, i);
      if (Panel->GetChildCount(child) == 5) {
        spectator = child;
        break;
      }
    }
  }
  if (!spectator)
    spectator = structural;
  if (!spectator)
    return false;
  void *client = Panel->Client(spectator);
  if (!client)
    return false;
  SpectatorPanelVPanel = spectator;
  SpectatorPanelClient = client;
  installClientPanelPaintHook(client);
  return true;
}

// The selective paint filter. No IsVisible/IsPopup reads (the original's own
// decision logic stays untouched for everything allowed); the module string
// is the whole rule and must match the install-reported identities exactly
// (T02 §6: "GameUI" / "ServerBrowser"; never "GAMEUIDLL").
void __fastcall H_PaintTraverse(void *pthis, int dummy, vgui::VPANEL vguiPanel,
                                bool forceRepaint, bool allowForce) {
  // The engine paints from one root; remember it when the surface interface
  // is not available (the walk then starts here instead).
  if (!RootVPanel && Panel && vguiPanel && !Panel->GetParent(vguiPanel))
    RootVPanel = vguiPanel;
  // The observer overlay is a client VGUI child, not a GameUI module panel:
  // it yields to the same per-element claim as the gHUD list (the module
  // returns zero while its replacement document is unavailable).
  if (SpectatorPanelVPanel && vguiPanel == SpectatorPanelVPanel &&
      spectatorClaimed()) {
    // Hide the panel as well: the client-panel virtual below skips it, and a
    // hidden panel also drops out of every traversal and hit test path.
    if (Panel->IsVisible(vguiPanel))
      Panel->SetVisible(vguiPanel, false);
    uint32_t cleared = 0;
    clearNativeInput(vguiPanel, cleared);
    if (cleared)
      services::nativeUiBridge().noteInputFlagsCleared(cleared);
    return;
  }
  const char *module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
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
vgui::VPANEL __fastcall H_IsWithinTraverse(void *pthis, int dummy,
                                           vgui::VPANEL vguiPanel, int x, int y,
                                           bool traversePopups) {
  const char *module = Panel ? Panel->GetModuleName(vguiPanel) : nullptr;
  if (services::nativeUiBridge().gateHitTestAttempt(module))
    return 0;
  if (OriginalIsWithinTraverse)
    return OriginalIsWithinTraverse(pthis, dummy, vguiPanel, x, y,
                                    traversePopups);
  return 0;
}

// Anything that can be undone without the hook objects (pre-hook failures).
void failCleanup() {
  Panel = nullptr;
  Surface = nullptr;
  OriginalPaintTraverse = nullptr;
  OriginalIsWithinTraverse = nullptr;
}

const char *const kSurfaceVersions[] = {
    "VGUI_Surface026", "VGUI_Surface025", "VGUI_Surface024",
    "VGUI_Surface023", "VGUI_Surface022", "VGUI_Surface021",
    "VGUI_Surface020", "VGUI_Surface019",
};

} // namespace

void paintGateFrame() {
  if (!Installed)
    return;
  const bool claimed = spectatorClaimed();
  // The walk installs the client-panel hook on the first located bar and
  // keeps the identity current while the replacement owns the surface.
  if (!ClientPaintHook || claimed) {
    if (refreshSpectatorPanel()) {
      FailedWalks = 0;
    } else if (claimed) {
      // One report per ~3 s of failed walks: the identity rules (gViewPort or
      // the structural form) did not match this client build.
      if (++FailedWalks == 180) {
        FailedWalks = 0;
        abc::ConsolePrint(
            "[AbcUi] paint gate: observer bar not located (surface=%d "
            "root=%p)\n",
            Surface ? 1 : 0, reinterpret_cast<void *>(RootVPanel));
      }
    } else {
      FailedWalks = 0;
    }
  }
}

void installPaintGate() {
  if (Installed)
    return;

  // Step 1: the vgui2 module and its image range. The slot identity checks
  // below need both; a missing module is fatal (the strict seam cannot be
  // established without it).
  const HMODULE vgui2 = GetModuleHandleA("vgui2.dll");
  if (!vgui2)
    abc::SysError("[AbcUi] paint gate: vgui2.dll not loaded; cannot establish "
                  "native UI suppression");
  const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(vgui2);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    abc::SysError("[AbcUi] paint gate: vgui2.dll image header invalid");
  const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
      reinterpret_cast<const BYTE *>(vgui2) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    abc::SysError("[AbcUi] paint gate: vgui2.dll NT header invalid");
  const auto imageBase = reinterpret_cast<const BYTE *>(vgui2);
  const auto imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
  VguiImageBase = reinterpret_cast<const std::byte *>(imageBase);
  VguiImageEnd = reinterpret_cast<const std::byte *>(imageEnd);

  // Step 2: the IPanel interface through the vgui2 factory.
  if (!g_pMetaHookAPI)
    abc::SysError("[AbcUi] paint gate: metahook API unavailable");
  CreateInterfaceFn getFactory =
      Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(vgui2));
  if (!getFactory)
    abc::SysError("[AbcUi] paint gate: vgui2.dll has no factory");
  auto *panel = static_cast<vgui::IPanel *>(
      getFactory(VGUI_PANEL_INTERFACE_VERSION, nullptr));
  if (!panel) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: \"%s\" unavailable from vgui2.dll",
                  VGUI_PANEL_INTERFACE_VERSION);
  }

  // Optional: the vgui2 surface, whose embedded panel is the root of the walk
  // that locates the client observer bar. The interface version follows the
  // engine build, so the known generations are probed in order; a miss only
  // disables the walk (the IPanel gate stays armed).
  for (const char *version : kSurfaceVersions) {
    Surface = static_cast<vgui::ISurface *>(getFactory(version, nullptr));
    if (Surface)
      break;
  }

  // Optional anchor for the client viewport: gViewPort is a standard client
  // global in the shared catalog. A missing symbol degrades the walk to its
  // structural form; the gate itself stays strict.
  if (g_pMetaHookAPI->IsGameSymbolAvailable(abc::g_dwClientBase, "gViewPort") ==
      MH_GAMESYMBOL_OK)
    ViewportSlot = static_cast<void **>(abc::ResolveGameSymbol(
        abc::g_dwClientBase, "client", "gViewPort",
        MH_GAMESYMBOL_KIND_GLOBAL));

  // Step 3: query the binary-matched slots before validating their owners.
  // MetaHook omits slot-only virtualFunction declarations from its runtime
  // catalog; the local scalar preserves the published PaintTraverse index.
  const auto paintIndex =
      abc::GameScalar(vgui2, "AbcUi_IPanel_PaintTraverse_index");
  const auto hitTestIndex =
      abc::GameScalar(vgui2, "AbcUi_IPanel_IsWithinTraverse_index");
  if (paintIndex > 255 || hitTestIndex > 255)
    abc::SysError("[AbcUi] paint gate: invalid gamedata virtual slot");
  void **vtable = *reinterpret_cast<void ***>(panel);
  const void *paintEntry = vtable[paintIndex];
  const void *hitTestEntry = vtable[hitTestIndex];
  if (paintEntry < imageBase || paintEntry >= imageEnd) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: IPanel PaintTraverse (%p) outside the "
                  "vgui2 image [%p..%p); layout drifted",
                  paintEntry, imageBase, imageEnd);
  }
  if (hitTestEntry < imageBase || hitTestEntry >= imageEnd) {
    failCleanup();
    abc::SysError("[AbcUi] paint gate: IPanel IsWithinTraverse (%p) outside "
                  "the vgui2 image; layout drifted",
                  hitTestEntry, imageBase, imageEnd);
  }

  // Step 4: install both hooks. The bridge stays unarmed until this
  // succeeded, so a failed install cannot have suppressed anything yet.
  void *originalTraverse = nullptr;
  hook_t *traverseHook = g_pMetaHookAPI->VFTHook(
      panel, 0, paintIndex, reinterpret_cast<void *>(&H_PaintTraverse),
      &originalTraverse);
  void *originalHitTest = nullptr;
  hook_t *hitTestHook =
      traverseHook
          ? g_pMetaHookAPI->VFTHook(
                panel, 0, hitTestIndex,
                reinterpret_cast<void *>(&H_IsWithinTraverse), &originalHitTest)
          : nullptr;
  if (!traverseHook || !originalTraverse || !hitTestHook || !originalHitTest) {
    if (traverseHook)
      g_pMetaHookAPI->UnHook(traverseHook);
    if (hitTestHook)
      g_pMetaHookAPI->UnHook(hitTestHook);
    failCleanup();
    abc::SysError(
        "[AbcUi] paint gate: VFTHook of IPanel gamedata slots failed");
  }

  Panel = panel;
  OriginalPaintTraverse =
      reinterpret_cast<decltype(OriginalPaintTraverse)>(originalTraverse);
  OriginalIsWithinTraverse =
      reinterpret_cast<decltype(OriginalIsWithinTraverse)>(originalHitTest);
  TraverseHook = traverseHook;
  HitTestHook = hitTestHook;
  Installed = true;
  services::nativeUiBridge().arm();
  abc::ConsolePrint("[AbcUi] paint gate: installed (IPanel gamedata slots, "
                    "surface %s)\n",
                    Surface ? "ready" : "unavailable");
}

void uninstallPaintGate() {
  // The bridge disarms only here: the strict takeover keeps suppressing
  // until the original UI can no longer paint (ClientVGUI::Shutdown is
  // the primary seam; GameUI::Shutdown/teardown are fallbacks, §9). Everything
  // below runs on the main thread inside that callback.
  services::nativeUiBridge().disarm();
  if (!Installed)
    return;
  // UnHook only reports "found" - at teardown MetaHook may already have
  // freed the hook records (MH_FreeAllHook restores the same vtable
  // entries), and the pointer comparison inside UnHook is safe for that.
  if (ClientPaintHook) {
    g_pMetaHookAPI->UnHook(ClientPaintHook);
    ClientPaintHook = nullptr;
    OriginalClientPaint = nullptr;
  }
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
  Surface = nullptr;
  ViewportSlot = nullptr;
  SpectatorPanelVPanel = 0;
  SpectatorPanelClient = nullptr;
  RootVPanel = 0;
  FailedWalks = 0;
  OriginalPaintTraverse = nullptr;
  OriginalIsWithinTraverse = nullptr;
  abc::ConsolePrint("[AbcUi] paint gate: uninstalled\n");
}

bool paintGateInstalled() {
  return Installed;
}

} // namespace rmlui
