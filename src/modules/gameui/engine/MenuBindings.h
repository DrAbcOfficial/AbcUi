#pragma once
// Internal MenuBindings seam; no engine SDK exposed.
#include "services/EngineUiBackend.h"
#include "services/creategame/CreateGameModel.h"
#include "services/playerlist/PlayerListModel.h"
namespace gameui::engine {
services::EngineUiBackend& menuBackend();
services::creategame::CreateGameModel::Seams createGameSeams();
services::playerlist::PlayerListModel::Seams playerListSeams();

// The character card's facts, read from the engine (the model/name cvars the
// player edits in Settings > Multiplayer). Empty strings when unregistered.
struct PlayerCardFacts {
	std::string PlayerName;
	std::string ModelName;   // as the cvar spells it ("HT_Cerberus")
	std::string ModelPath;   // resolvable Studio path for the <model> element
};
PlayerCardFacts playerCardFacts();
}
