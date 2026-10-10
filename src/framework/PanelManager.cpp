// framework::PanelManager implementation - see PanelManager.h.
#include "PanelManager.h"

#include "HudPanel.h"

#include "../panels/demo/DemoPanel.h"

#include <abc/Console.h>

namespace framework {

std::vector<std::unique_ptr<HudPanel>>& PanelManager::Panels() {
	// Factory list: one line per panel (plan 05 §4.1 - Viewport successor).
	static std::vector<std::unique_ptr<HudPanel>> panels = []() {
		std::vector<std::unique_ptr<HudPanel>> list;
		list.push_back(std::make_unique<panels::DemoPanel>());
		return list;
	}();
	return panels;
}

void PanelManager::initAll(Rml::Context* context) {
	if (!context)
		return;
	for (auto& panel : Panels())
		panel->loadDocument(context);
	abc::ConsolePrint("[AbcUi] %d panel(s) loaded\n", static_cast<int>(Panels().size()));
}

void PanelManager::vidInitAll() {
	// Batch A panels recompute scaled positions here (05 §3.1 size change).
}

void PanelManager::onFrame(double frameTime) {
	for (auto& panel : Panels())
		panel->onData(frameTime);
}

void PanelManager::shutdown() {
	Panels().clear();
}

}
