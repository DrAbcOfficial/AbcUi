// modules::HudPanelCatalog implementation - see HudPanelCatalog.h.
#include "HudPanelCatalog.h"

#include "../framework/Commands.h"
#include "../framework/HudPanel.h"
#include "../framework/PanelManager.h"
#include "../data/NetworkMessages.h"
#include "../data/TextFeed.h"
#include "../modsys/ModuleManager.h"
#include "../panels/chat/ChatPanel.h"

#include <memory>
#include <utility>
#include <vector>

namespace modules {
namespace {

std::vector<std::unique_ptr<framework::HudPanel>> buildPanels() {
  std::vector<std::unique_ptr<framework::HudPanel>> panels;
  // Batch B (feedback panels).
  // Batch C (interaction + big data).
  // Batch D (render depth).
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
  commands::registerAll();
  // Batch B.
  // Batch C.
  // Batch D.
  // Issue 13: after the client's HUD_Init ran (the hud_saytext_* pointers
  // are the client's own cvars).
  panels::chat::registerCvars();
}

} // namespace modules
