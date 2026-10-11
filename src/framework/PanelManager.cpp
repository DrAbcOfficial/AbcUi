// framework::PanelManager implementation - see PanelManager.h.
#include "PanelManager.h"

#include "Commands.h"
#include "HudPanel.h"

#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../panels/ammostack/AmmoStackPanel.h"
#include "../panels/ammobar/AmmoBarPanel.h"
#include "../panels/crosshair/CrosshairPanel.h"
#include "../panels/deadmsg/DeadMsgPanel.h"
#include "../panels/dmgtiles/DmgTilesPanel.h"
#include "../panels/effect/EffectPanel.h"
#include "../panels/flashlight/FlashlightPanel.h"
#include "../panels/gindicator/GIndicatorPanel.h"
#include "../panels/health/HealthPanel.h"
#include "../panels/indicator/IndicatorPanel.h"
#include "../panels/itemstack/ItemStackPanel.h"
#include "../panels/notice/NoticePanel.h"
#include "../panels/popnum/PopNumPanel.h"
#include "../panels/sidetext/SideTextPanel.h"
#include "../panels/textmenu/TextMenuPanel.h"
#include "../panels/weaponstack/WeaponStackPanel.h"

#include <abc/Console.h>

namespace framework {

std::vector<std::unique_ptr<HudPanel>>& PanelManager::Panels() {
	// Factory list: one line per panel (plan 05 §4.1 - Viewport successor).
	static std::vector<std::unique_ptr<HudPanel>> panels = []() {
		std::vector<std::unique_ptr<HudPanel>> list;
		list.push_back(std::make_unique<panels::HealthPanel>());
		list.push_back(std::make_unique<panels::FlashlightPanel>());
		list.push_back(std::make_unique<panels::AmmoBarPanel>());
		list.push_back(std::make_unique<panels::AmmoStackPanel>());
		list.push_back(std::make_unique<panels::ItemStackPanel>());
		list.push_back(std::make_unique<panels::WeaponStackPanel>());
		list.push_back(std::make_unique<panels::DeadMsgPanel>());
		list.push_back(std::make_unique<panels::NoticePanel>("notice", "abcui/rml/notice.rml", textfeed::kPrintNotify));
		list.push_back(std::make_unique<panels::NoticePanel>("notice-center", "abcui/rml/notice_center.rml", textfeed::kPrintCenter));
		list.push_back(std::make_unique<panels::SideTextPanel>());
		list.push_back(std::make_unique<panels::TextMenuPanel>());
		// Batch B (feedback panels, plan 05 §6).
		list.push_back(std::make_unique<panels::CrosshairPanel>());
		list.push_back(std::make_unique<panels::IndicatorPanel>());
		list.push_back(std::make_unique<panels::GIndicatorPanel>());
		list.push_back(std::make_unique<panels::DmgTilesPanel>());
		list.push_back(std::make_unique<panels::EffectPanel>());
		list.push_back(std::make_unique<panels::PopNumPanel>());
		return list;
	}();
	return panels;
}

void PanelManager::registerPanelCvars() {
	// HUD_Init-time cvar registration (plan 08 §4.9); panel namespaces own
	// their cvar pointers, the spine only triggers the batch.
	panels::health::registerCvars();
	panels::deadmsg::registerCvars();
	panels::sidetext::registerCvars();
	commands::registerAll();
	// Batch B.
	panels::crosshair::registerCvars();
	panels::indicator::registerCvars();
	panels::gindicator::registerCvars();
	panels::effect::registerCvars();
	panels::popnum::registerCvars();
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

void PanelManager::onRedraw(double frameTime) {
	for (auto& panel : Panels())
		panel->onRedraw(frameTime);
}

void PanelManager::shutdown() {
	Panels().clear();
}

}
