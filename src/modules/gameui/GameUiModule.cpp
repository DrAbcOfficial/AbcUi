// Extracted from GameUiModule.cpp (UTF-8). Responsibility: GameUiModule.
#include "modules/gameui/GameUiModule.h"
#include "modules/gameui/settings/SettingsResources.h"
#include "modules/gameui/debug/DebugCommands.h"
#include <abc/Console.h>
#include "modules/gameui/engine/MenuBindings.h"
#include "modules/gameui/engine/SettingsBindings.h"
#include "modules/gameui/engine/VideoBindings.h"
namespace gameui {
using namespace engine;
using namespace settings;
GameUiModule::GameUiModule()
	: Controller(&menuBackend()), CaptureOwner(this) {
	SettingsModel.setSeams(settingsSeams());
	BindModel.setSeams(bindSeams());
	VideoModel.setSeams(videoSeams());
	VoiceModel.setSeams(voiceSeams());
	// T19: the create-game launcher (typed cvar writes + the shipped
	// command sequence) and its backend relay. The thunk reaches the
	// module's request value object through the Instance pointer, the
	// same shape the Debug diagnostics use.
	CreateGame.setSeams(createGameSeams());
	menuBackend().setLocalGameLauncher(&GameUiModule::launchLocalGame);
	// T20: the player-list model's roster/mute seams (playerres + the
	// shipped GameClientExports001 probe).
	PlayerList.setSeams(playerListSeams());
	modules::registry().add(this);
	services::nativeUiBridge().setMenuSink(this);
	Instance = this;
}


const char* GameUiModule::name() const { return "gameui"; }


void GameUiModule::onInit() {
	// T17: the server data provider. The Steam adapter resolves its
	// interface lazily (Steam may not be up yet at HUD_Init) and is a
	// process-lifetime singleton; the model owns the async lifecycle.
	ServerBrowser.setProvider(services::serverbrowser::steamQueryProvider());
	// T13 description table: the first batch + the advanced rows from the
	// live (localized) user.scr - the shipped script is the same source
	// the original advanced dialog parses. A failed parse keeps the
	// static mirror of the shipped script (buildAdvancedTable), so the
	// table is never empty. Must run before the first snapshot (page
	// present) - the model owns row POINTERS into SettingsTable/Advanced,
	// both module members that outlive the model.
	Advanced = buildAdvancedTable();
	SettingsTable.assign(services::settings::FirstBatchRows,
		services::settings::FirstBatchRows + services::settings::FirstBatchCount);
	SettingsTable.insert(SettingsTable.end(), Advanced.rows.begin(), Advanced.rows.end());
	SettingsModel.setTable(SettingsTable.data(), static_cast<int>(SettingsTable.size()));
	// The multiplayer enumerations: directories (models/player) and the
	// spray basenames (logos) - the same scans the shipped list fills
	// run; empty scans leave the rows readable but unchangeable (the
	// live engine value stays selectable).
	SettingsModel.setStringOptions("model", scanModDirectories("models/player"));
	SettingsModel.setStringOptions("cl_logofile", scanModFileBasenames("logos"));
	// T11 bind sources live in the mod dir (engine-owned data, not abcui
	// assets): the action list, the factory defaults and the label table.
	// Missing files degrade to an empty action table - the settings page
	// renders the bind section as unavailable (fail-closed).
	if (!BindModel.loadActions(readModFile("gfx/shell/kb_act.lst").c_str()))
		abc::ConsoleWarn("[AbcUi] kb_act.lst malformed - key bindings unavailable\n");
	BindModel.loadDefaults(readModFile("gfx/shell/kb_def.lst").c_str());
	BindModel.loadLabels(readModFile("resource/valve_english.txt").c_str());
	// T19: the map catalog. The shipped create-game page enumerates
	// IFileSystem::FindFirst("maps/*.bsp") over the MOUNTED search paths
	// (GameUI.dll FUN_10005630 with pathID NULL + "GAMEDOWNLOAD"); the
	// same call here so content mounted from any search path is seen, not
	// just one disk directory. Unreadable/missing -> empty catalog (the
	// page then refuses every request fail-closed with CG_ErrorUnavailable).
	loadMapCatalog();
#ifndef NDEBUG
	DebugCommands::registerCommands();
#endif
}


void GameUiModule::onShutdown() {
	// Context still alive here: every document closes and drops its
	// listeners before the DOM dies (module contract). The tokens
	// release first - a stale no-op when the router already reset.
	// The mic test is a device resource: stop it before anything tears
	// down (the pairing guarantee covers the shutdown path too).
	VoiceModel.stopTest();
	CaptureToken.release();
	PageToken.release();
	// Stop callbacks and release queries before any observing document dies.
	ServerBrowser.closeContext();
	Shell.reset();
	SubStub.reset();
	Console.reset();
	Settings.reset();
	Browser.reset();
	CreateGamePage_.reset();
	PlayerListPage_.reset();
	VideoConfirm.reset();
	ConfirmQuit.reset();
	Loading.reset();
	ConnError.reset();
	Password.reset();
	DirectConnect.reset();
	Context = nullptr;
	MenuPresented = false;
	SettingsPresented = false;
	ConsolePresented = false;
	VideoConfirmPresented = false;
	LoadingVisible = false;
	ErrorPresented = false;
	PasswordPresented = false;
	PasswordReprepare = false;
	DirectConnectPresented = false;
	CreateGamePresented = false;
	PlayerListPresented = false;
	LastStubPage = MenuPage::kMain;
}

GameUiModule* GameUiModule::Instance = nullptr;
namespace { GameUiModule RegisterModule; }

}
