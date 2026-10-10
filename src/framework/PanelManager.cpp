// framework::PanelManager implementation - see PanelManager.h.
#include "PanelManager.h"

#include "HudPanel.h"

#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../panels/ammostack/AmmoStackPanel.h"
#include "../panels/ammobar/AmmoBarPanel.h"
#include "../panels/deadmsg/DeadMsgPanel.h"
#include "../panels/demo/DemoPanel.h"
#include "../panels/flashlight/FlashlightPanel.h"
#include "../panels/health/HealthPanel.h"
#include "../panels/itemstack/ItemStackPanel.h"
#include "../panels/notice/NoticePanel.h"
#include "../panels/weaponstack/WeaponStackPanel.h"

#include <abc/Console.h>

namespace framework {

std::vector<std::unique_ptr<HudPanel>>& PanelManager::Panels() {
	// Factory list: one line per panel (plan 05 §4.1 - Viewport successor).
	static std::vector<std::unique_ptr<HudPanel>> panels = []() {
		std::vector<std::unique_ptr<HudPanel>> list;
		list.push_back(std::make_unique<panels::DemoPanel>());
		list.push_back(std::make_unique<panels::HealthPanel>());
		list.push_back(std::make_unique<panels::FlashlightPanel>());
		list.push_back(std::make_unique<panels::AmmoBarPanel>());
		list.push_back(std::make_unique<panels::AmmoStackPanel>());
		list.push_back(std::make_unique<panels::ItemStackPanel>());
		list.push_back(std::make_unique<panels::WeaponStackPanel>());
		list.push_back(std::make_unique<panels::DeadMsgPanel>());
		list.push_back(std::make_unique<panels::NoticePanel>("notice", "abcui/rml/notice.rml", 1));
		list.push_back(std::make_unique<panels::NoticePanel>("notice-center", "abcui/rml/notice_center.rml", 4));
		return list;
	}();
	return panels;
}

void PanelManager::registerPanelCvars() {
	// HUD_Init-time cvar registration (plan 08 §4.9); panel namespaces own
	// their cvar pointers, the spine only triggers the batch.
	panels::health::registerCvars();
	panels::deadmsg::registerCvars();
}

void PanelManager::initAll(Rml::Context* context) {
	if (!context)
		return;
	for (auto& panel : Panels())
		panel->loadDocument(context);
	abc::ConsolePrint("[AbcUi] %d panel(s) loaded\n", static_cast<int>(Panels().size()));
}

void PanelManager::vidInitAll() {
	// Old Viewport::VidInit ended with Reset(): every panel back to its
	// pre-map state on every map load / video init.
	for (auto& panel : Panels())
		panel->reset();
}

void PanelManager::onFrame(double frameTime) {
	for (auto& panel : Panels())
		panel->onData(frameTime);
}

void PanelManager::shutdown() {
	Panels().clear();
}

}
