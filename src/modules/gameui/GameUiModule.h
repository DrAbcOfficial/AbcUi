#pragma once
#include "navigation/MenuAction.h"
#include "../../framework/UiInputRouter.h"
// GameUI composition root and cross-domain navigation coordinator. Platform
// binding, resource loading, diagnostics and context query ownership are
// separate collaborators. Domain flow implementations live beside their
// peers; this header is internal to the gameui module, never a spine API.
#include "../Module.h"
#include "../../panels/console/ConsolePage.h"
#include "../../panels/creategame/CreateGamePage.h"
#include "../../panels/loading/LoadingWindows.h"
#include "../../panels/mainmenu/MainMenuPage.h"
#include "services/uiconfig/ConfigRepository.h"
#include "../../panels/menushared/MenuShellWindows.h"
#include "../../panels/playerlist/PlayerListPage.h"
#include "../../panels/serverbrowser/ServerBrowserPage.h"
#include "../../panels/settings/SettingsPage.h"
#include "../../panels/settings/VideoConfirmPage.h"
#include "../../services/EngineUiBackend.h"
#include "../../services/GameUiBridge.h"
#include "../../services/creategame/CreateGameModel.h"
#include "../../services/loading/LoadingModel.h"
#include "../../services/loading/AutoRetryGate.h"
#include "../../services/playerlist/PlayerListModel.h"
#include "../../services/friends/FriendsModel.h"
#include "../../services/serverbrowser/ServerFavorites.h"
#include "../../services/serverbrowser/ServerListModel.h"
#include "../../services/serverbrowser/ServerQuery.h"
#include "../../services/settings/KeyBindModel.h"
#include "../../services/settings/SettingsModel.h"
#include "../../services/settings/VideoModeModel.h"
#include "../../services/settings/VoiceModel.h"
#include "navigation/MenuController.h"
#include "../../services/serverbrowser/ServerBrowserSession.h"
#include "settings/SettingsResources.h"
#include <memory>
#include <vector>
#include <string>
namespace gameui {
using menu::EventKind;
using menu::MenuEvent;
using menu::MenuOverlay;
using menu::MenuPage;
class DebugCommands;
class GameUiModule final : public modules::IUiModule,
    private framework::uiinput::Owner, private services::INativeMenuSink {
public:
	GameUiModule();
	const char* name() const override;
	void onInit() override;
	void onFrame(double frameTime) override;
	void onContextReady(Rml::Context* context) override;
	void onShutdown() override;
private:
	friend class DebugCommands;
	static void logLoadOutcome(framework::InteractiveDocument::LoadOutcome outcome);
	void startKeyCapture();
	void endKeyCapture();
	void onCapturedKeyEdge(int keynum);
	void onKeyCaptureForceLost();
	void serviceEmergencyPages();
	void onNativeMenuShow() override;
	void onNativeMenuHide() override;
	void onNativeConsoleOpen() override;
	void onNativeConsoleClose() override;
	void presentMenu();
	void dismissMenu();
	void reconcilePhase();
	void syncMenuPresentation();
	void presentSettingsPage();
	void presentServerBrowserPage();
	void presentCreateGamePage();
	void loadMapCatalog();
	void applyCreateGame();
	static const char* hintTokenFor(const services::creategame::Validation& validation);
	void presentPlayerListPage();
	void selectPlayerRow(const services::playerlist::SelectionKey& Key);
	void togglePlayerMute();
	void beginServerQuery(services::serverbrowser::QueryKind kind);
	void connectSelectedServer();
	void toggleSelectedFavorite();
	void resolveVideoRecovery();
	void applyVideoChange();
	void raiseVideoConfirmation();
	void closeVideoConfirmation();
	void syncVideoConfirmPage();
	void tickVideoTimeout();
	void releaseServerPageQueries();
	void logServerQueryChange();
	void showStubPage(panels::menushared::MenuStubPage* stub, MenuPage page, bool console);
	void onMenuAction(const char* action);
	bool retryServerAttempt();
	void tickAutoRetry();
	void handleCreateGameAction(navigation::ActionKind Kind);
	void handlePlayerListAction(navigation::ActionKind Kind);
	void handleConnectionAction(navigation::ActionKind Kind);
	void handleVideoAction(navigation::ActionKind Kind);
	void handleServerAction(navigation::ActionKind Kind);
	void onFriendJoin(uint64_t steamId, const char* address);

	void finishDispatch();
	bool consumeLaunchFailure();
	static bool launchLocalGame(const char* map, const char** reasonToken);
	void syncAttemptState();
	void drainLoadingFeed();
	void recordPendingHistory();
	void hideAllMenuDocuments();
	void focusLog(const char* phase);
	void pumpBackgroundWork();
	void pumpVisibleWork();
	bool claimsKey(int down, int keynum) override;
	bool claimsWheel() override;
	void onClaimedKeyDown(int keynum) override;
	void onOwnershipLost() override;
	class KeyCaptureOwner final : public framework::uiinput::Owner {
	public:
		explicit KeyCaptureOwner(GameUiModule* owner)
			: Owner(owner) {}
		bool claimsKey(int down, int keynum) override {
			(void)down;
			(void)keynum;
			return true;
		}
		bool claimsWheel() override { return true; }
		void onClaimedKeyDown(int keynum) override { Owner->onCapturedKeyEdge(keynum); }
		bool onClaimedWheelPulse(int keynum) override {
			Owner->onCapturedKeyEdge(keynum);
			return true; // the pulse is consumed - no page scroll
		}
		void onOwnershipLost() override { Owner->onKeyCaptureForceLost(); }

	private:
		GameUiModule* Owner;
	};
	static GameUiModule* Instance;
	Rml::Context* Context = nullptr;
	gameui::menu::MenuController Controller;
	services::uiconfig::ConfigRepository UiConfig;
	services::settings::SettingsModel SettingsModel;
	services::settings::KeyBindModel BindModel;
	services::settings::VoiceModel VoiceModel;
	services::settings::VideoModeModel VideoModel;
	services::loading::LoadingModel LoadingModelState;
	services::serverbrowser::ServerBlacklist Blacklist;
	services::serverbrowser::ServerBrowserSession ServerBrowser;
	services::creategame::CreateGameModel CreateGame;
	services::creategame::Request PendingCreateGame;
	bool PendingCreateGameValid = false;
	bool CurrentAttemptIsLocal = false;
	bool LastAttemptWasLocal = false;
	const char* PendingLaunchFailure = nullptr;
	bool CreateGamePresented = false;
	services::playerlist::PlayerListModel PlayerList;
	uint32_t FriendsProjected = 0;
	// Debug diagnostics (abc_ui_menu_model): the last module frame delta, so a
	// stalled pump is distinguishable from a stalled refresh.
	double LastFrameTime = 0.0;
	bool PlayerListPresented = false;
	services::serverbrowser::FavoriteKey PendingHistory;
	bool PendingHistoryValid = false;
	// 自动重试 (the browser's toggle): a failed join attempt re-enters the
	// existing typed retry path, bounded per failure chain. The page owns the
	// toggle's truth; the gate owns the run policy.
	services::loading::AutoRetryGate AutoRetry;
	std::unique_ptr<panels::mainmenu::MainMenuPage> Shell;
	std::unique_ptr<panels::menushared::MenuStubPage> SubStub;
	std::unique_ptr<panels::console::ConsolePage> Console;
	std::unique_ptr<panels::settings::SettingsPage> Settings;
	std::unique_ptr<panels::serverbrowser::ServerBrowserPage> Browser;
	std::unique_ptr<panels::creategame::CreateGamePage> CreateGamePage_;  // T19 page
	std::unique_ptr<panels::playerlist::PlayerListPage> PlayerListPage_;  // T20 page
	std::unique_ptr<panels::settings::VideoConfirmPage> VideoConfirm;
	std::unique_ptr<panels::menushared::ConfirmQuitOverlay> ConfirmQuit;
	std::unique_ptr<panels::loading::LoadingPage> Loading;
	std::unique_ptr<panels::loading::ConnectionErrorOverlay> ConnError;
	std::unique_ptr<panels::loading::PasswordOverlay> Password;
	std::unique_ptr<panels::loading::DirectConnectOverlay> DirectConnect;
	framework::uiinput::Token PageToken;
	KeyCaptureOwner CaptureOwner;
	framework::uiinput::Token CaptureToken;
	bool MenuPresented = false;
	bool SettingsPresented = false;
	bool ConsolePresented = false;
	bool VideoConfirmPresented = false;
	bool LoadingVisible = false;
	bool ErrorPresented = false;
	bool PasswordPresented = false;
	bool DirectConnectPresented = false;
	bool PasswordInvalid = false;
	int PasswordPrompts = 0;
	bool PasswordReprepare = false;
	std::string LastErrorReason;
	int VideoCountdownShown = -1;
	uint32_t NextPhaseSession = 1;
	MenuPage LastStubPage = MenuPage::kMain;
};
}
