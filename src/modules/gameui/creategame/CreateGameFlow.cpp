// Extracted from GameUiModule.cpp (UTF-8). Responsibility: creategame/CreateGameFlow.
#include "modules/gameui/GameUiModule.h"
#include <abc/Plugin.h>
#include <abc/Console.h>
#include <abc/CvarCmd.h>
#include <cstdio>
#include <utility>
#include "modules/gameui/navigation/MenuAction.h"
namespace gameui {
using namespace navigation;
void GameUiModule::presentCreateGamePage() {
	if (!CreateGamePage_)
		return;
	if (!CreateGamePresented) {
		const char* liveName = abc::CVAR_GET_STRING("hostname");
		CreateGamePage_->prepare(CreateGame.catalog(), liveName ? liveName : "");
		CreateGamePresented = true;
	}
	CreateGamePage_->show();
}


void GameUiModule::loadMapCatalog() {
	services::creategame::MapCatalog catalog;
	if (abc::g_pFileSystem) {
		const char* pathIds[] = {nullptr, "GAMEDOWNLOAD"};
		for (const char* pathId : pathIds) {
			FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
			const char* name = abc::g_pFileSystem->FindFirst("maps/*.bsp", &handle, pathId);
			while (name) {
				catalog.addRawName(name);
				name = abc::g_pFileSystem->FindNext(handle);
			}
			if (handle != FILESYSTEM_INVALID_FIND_HANDLE)
				abc::g_pFileSystem->FindClose(handle);
		}
	} else {
		abc::ConsoleWarn("[AbcUi] map catalog: engine filesystem unavailable\n");
	}
	catalog.finalize();
	CreateGame.setCatalog(std::move(catalog));
#ifndef NDEBUG
	abc::ConsolePrint("[AbcUi] map catalog: %d maps\n",
		static_cast<int>(CreateGame.catalog().size()));
#endif
}


void GameUiModule::applyCreateGame() {
	if (!CreateGamePage_)
		return;
	if (CreateGame.catalog().size() == 0) {
		// No enumerated maps at all (filesystem unavailable): the request
		// cannot be valid, name the real cause.
		CreateGamePage_->showHint("#CG_ErrorUnavailable");
		return;
	}
	services::creategame::Request request;
	request.Map = CreateGamePage_->selectedMap();
	request.ServerName = CreateGamePage_->serverName();
	request.Password = CreateGamePage_->password();
	int players = 0;
	if (!services::creategame::CreateGameModel::parsePlayers(
			CreateGamePage_->playersText().c_str(), players)) {
		CreateGamePage_->showHint("#CG_ErrorPlayers");
		return;  // nothing reaches the controller or the engine
	}
	request.MaxPlayers = players;
	const auto validation =
		services::creategame::CreateGameModel::validate(CreateGame.catalog(), request);
	if (!validation.ok()) {
		CreateGamePage_->showHint(hintTokenFor(validation));
		return;  // nothing reaches the controller or the engine
	}
	PendingCreateGame = request;
	PendingCreateGameValid = true;
	Controller.dispatch(gameui::menu::makeCreateGameRequest(request.Map.c_str()));
	finishDispatch();
}


const char* GameUiModule::hintTokenFor(const services::creategame::Validation& validation) {
	using services::creategame::Error;
	using services::creategame::Field;
	switch (validation.error) {
	case Error::kPlayersRange:
		return "#CG_ErrorPlayers";
	case Error::kPasswordLength:
	case Error::kPasswordCharset:
		return "#CG_ErrorPassword";
	case Error::kNameEmpty:
	case Error::kNameLength:
	case Error::kNameCharset:
		return "#CG_ErrorName";
	default:
		(void)validation.field;
		return "#CG_ErrorMap";
	}
}


bool GameUiModule::consumeLaunchFailure() {
	if (!PendingLaunchFailure)
		return false;
	const char* reason = PendingLaunchFailure;
	PendingLaunchFailure = nullptr;
	const uint32_t live = LoadingModelState.generation();
	if (live != 0) {
		Controller.dispatch(gameui::menu::makeLoadingFinished(live,
			gameui::menu::LoadingOutcome::kFailed, reason));
		return true;
	}
	return false;
}


bool GameUiModule::launchLocalGame(const char* map, const char** reasonToken) {
	if (reasonToken)
		*reasonToken = "#CG_ErrorInternal";
	if (!Instance || !reasonToken)
		return false;
	GameUiModule& self = *Instance;
	if (!self.PendingCreateGameValid || !map
		|| self.PendingCreateGame.Map != map) {
		abc::ConsoleWarn("[AbcUi] local game launch mismatch (no kept request)\n");
		return false;
	}
	switch (self.CreateGame.launch(self.PendingCreateGame)) {
	case services::creategame::CreateGameModel::Launch::kLaunched:
		return true;
	case services::creategame::CreateGameModel::Launch::kInvalid:
		*reasonToken = "#CG_ErrorInternal";  // validated before the dispatch; defensive
		return false;
	case services::creategame::CreateGameModel::Launch::kWriteFailed:
		*reasonToken = "#CG_ErrorWrite";
		return false;
	case services::creategame::CreateGameModel::Launch::kUnwired:
		*reasonToken = "#CG_ErrorInternal";
		return false;
	}
	return false;
}

void GameUiModule::handleCreateGameAction(ActionKind Kind) {
	// T19 page-internal intents (fixed names only): Apply assembles
	// and validates the request; Cancel is a plain back with zero
	// engine writes (the page never held any).
	if (Kind == ActionKind::kCgApply)
		applyCreateGame();
	else if (Kind == ActionKind::kCgCancel)
		Controller.dispatch(MenuEvent{EventKind::kBack});
	else {
		abc::ConsoleWarn("[AbcUi] unknown create-game action \"%s\" dropped\n", actionName(Kind));
		return;
	}
	finishDispatch();
	return;

}

}
