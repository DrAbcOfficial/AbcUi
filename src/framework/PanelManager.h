#pragma once

// framework::PanelManager - panel registry + lifecycle, the successor of the
// old Viewport god object's "hold and route 25 panels" duty (plan 05 §4.1).
//
// Panels self-register by appending their factory to the built-in list in
// PanelManager.cpp (Phase 4+ adds one line per batch panel - the old
// Viewport constructor did all of them inline). The manager owns panel
// lifetime and routes the HUD pump.

#include <RmlUi/Core/Context.h>

#include <memory>
#include <vector>

namespace framework {

class HudPanel;

class PanelManager {
public:
	// HUD_Init-time cvar registration for the batch panels (plan 08 §4.9).
	static void registerPanelCvars();
	// Creates all registered panels and loads their documents.
	static void initAll(Rml::Context* context);
	// HUD_VidInit: resolution changes + per-panel map reset.
	static void vidInitAll();
	// HUD_Frame: per-panel data refresh.
	static void onFrame(double frameTime);
	static void shutdown();

	static const std::vector<std::unique_ptr<HudPanel>>& panels() { return Panels(); }

private:
	static std::vector<std::unique_ptr<HudPanel>>& Panels();
};

}
