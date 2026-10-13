#pragma once
// Internal MenuBindings seam; no engine SDK exposed.
#include "services/EngineUiBackend.h"
#include "services/creategame/CreateGameModel.h"
#include "services/playerlist/PlayerListModel.h"
namespace gameui::engine {
services::EngineUiBackend& menuBackend();
services::creategame::CreateGameModel::Seams createGameSeams();
services::playerlist::PlayerListModel::Seams playerListSeams();
}
