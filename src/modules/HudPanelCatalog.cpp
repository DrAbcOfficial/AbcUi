// modules::HudPanelCatalog implementation - see HudPanelCatalog.h.
#include "HudPanelCatalog.h"

#include "../framework/Commands.h"
#include "../framework/HudPanel.h"
#include "../framework/PanelManager.h"
#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../modsys/ModuleManager.h"
#include "../panels/ammobar/AmmoBarPanel.h"
#include "../panels/ammostack/AmmoStackPanel.h"
#include "../panels/chat/ChatPanel.h"
#include "../panels/crosshair/CrosshairPanel.h"
#include "../panels/deadmsg/DeadMsgPanel.h"
#include "../panels/dmgtiles/DmgTilesPanel.h"
#include "../panels/effect/EffectPanel.h"
#include "../panels/gindicator/GIndicatorPanel.h"
#include "../panels/health/HealthPanel.h"
#include "../panels/indicator/IndicatorPanel.h"
#include "../panels/itemhighlight/ItemHighlightPanel.h"
#include "../panels/itemstack/ItemStackPanel.h"
#include "../panels/motd/MotdPanel.h"
#include "../panels/notice/NoticePanel.h"
#include "../panels/playerboard/PlayerBoardPanel.h"
#include "../panels/popnum/PopNumPanel.h"
#include "../panels/radar/RadarPanel.h"
#include "../panels/scoreboard/ScoreboardPanel.h"
#include "../panels/sidetext/SideTextPanel.h"
#include "../panels/weaponchoose/WeaponChoosePanel.h"
#include "../panels/weaponstack/WeaponStackPanel.h"

#include <memory>
#include <utility>
#include <vector>

namespace modules {
namespace {

std::vector<std::unique_ptr<framework::HudPanel>> buildPanels() {
  std::vector<std::unique_ptr<framework::HudPanel>> panels;
  panels.push_back(std::make_unique<panels::HealthPanel>());
  panels.push_back(std::make_unique<panels::AmmoBarPanel>());
  panels.push_back(std::make_unique<panels::AmmoStackPanel>());
  panels.push_back(std::make_unique<panels::ItemStackPanel>());
  panels.push_back(std::make_unique<panels::WeaponStackPanel>());
  panels.push_back(std::make_unique<panels::DeadMsgPanel>());
  panels.push_back(std::make_unique<panels::NoticePanel>(
      "notice", "abc/abcui/hud/rml/notice.rml", textfeed::kPrintNotify));
  panels.push_back(std::make_unique<panels::NoticePanel>(
      "notice-center", "abc/abcui/hud/rml/notice_center.rml",
      textfeed::kPrintCenter));
  panels.push_back(std::make_unique<panels::SideTextPanel>());
  // Batch B (feedback panels).
  panels.push_back(std::make_unique<panels::CrosshairPanel>());
  panels.push_back(std::make_unique<panels::IndicatorPanel>());
  panels.push_back(std::make_unique<panels::GIndicatorPanel>());
  panels.push_back(std::make_unique<panels::DmgTilesPanel>());
  panels.push_back(std::make_unique<panels::EffectPanel>());
  panels.push_back(std::make_unique<panels::PopNumPanel>());
  // Batch C (interaction + big data).
  panels.push_back(std::make_unique<panels::PlayerBoardPanel>());
  panels.push_back(std::make_unique<panels::MotdPanel>());
  panels.push_back(std::make_unique<panels::ScoreboardPanel>());
  panels.push_back(std::make_unique<panels::WeaponChoosePanel>());
  // Batch D (render depth).
  panels.push_back(std::make_unique<panels::ItemHighlightPanel>());
  panels.push_back(std::make_unique<panels::RadarPanel>());
  // Issue 13: chat last, so its log/input paints over the readouts.
  panels.push_back(std::make_unique<panels::ChatPanel>());
  return panels;
}

} // namespace

void installHudPanels() {
  auto panels = buildPanels();
  // Module-registered panels join the tail (same order their packages
  // loaded in); the built-ins keep their fixed factory order ahead of them.
  for (auto &panel : framework::modmanager::takePanels())
    panels.push_back(std::move(panel));
  framework::PanelManager::installPanels(std::move(panels));
}

void registerHudPanelCvars() {
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
  // Batch C.
  panels::playerboard::registerCvars();
  panels::motd::registerCvars();
  panels::scoreboard::registerCvars();
  panels::weaponchoose::registerCvars();
  panels::weaponchoose::registerCommands();
  // Batch D.
  panels::itemhighlight::registerCvars();
  panels::radar::registerCvars();
  panels::radar::registerCommands();
  // Issue 13: after the client's HUD_Init ran (the hud_saytext_* pointers
  // are the client's own cvars).
  panels::chat::registerCvars();
}

} // namespace modules
