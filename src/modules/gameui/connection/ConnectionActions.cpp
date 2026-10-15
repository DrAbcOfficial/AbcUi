// Typed connection intents; no command parsing or UI visibility policy.
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include <string>
namespace gameui {
using namespace navigation;

namespace {
// The 自动重试 toggle's run policy: keep re-entering the SAME typed retry
// path, at most this many consecutive auto-retries per failure chain, one
// attempt every delay seconds. A chain ends on a confirmed join, a cancel,
// a manual retry, the toggle going off, or the budget running out; the
// honest error overlay stays up in every one of those cases.
constexpr int kAutoRetryMax = 3;
constexpr double kAutoRetryDelay = 5.0;
}

// The retry body shared by the overlay's 重试 intent and the auto-retry
// beat: a NEW attempt generation for the same kept target, never a reuse of
// the dead one. T19: a failed LOCAL attempt replays its kept request through
// the same typed path. False = nothing to retry (the overlay stays honest).
bool GameUiModule::retryServerAttempt() {
	if (PendingCreateGameValid && LastAttemptWasLocal) {
		Controller.dispatch(
			gameui::menu::makeCreateGameRequest(PendingCreateGame.Map.c_str()));
		finishDispatch();
		return true;
	}
	const std::string address = LoadingModelState.address();
	if (address.empty())
		return false;
	Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
	finishDispatch();
	return true;
}

// The per-frame auto-retry beat (pumpBackgroundWork). The browser page owns
// the toggle's truth; the timer runs on real frame time and fires only while
// the connection-error overlay is actually up.
void GameUiModule::tickAutoRetry() {
	const bool enabled = Browser && Browser->autoRetryJoin();
	const bool failed = Controller.isMenuActive()
		&& Controller.state().overlays.contains(MenuOverlay::kConnectionError);
	if (!enabled || !failed) {
		// Not trying: the next chain starts from a clean budget. (A fired
		// retry leaves this state too - the budget therefore only resets
		// on the terminal outcomes handled by the callers.)
		if (!failed) {
			AutoRetryCount = 0;
			AutoRetryDelay = -1.0;
		}
		return;
	}
	if (AutoRetryCount >= kAutoRetryMax) {
		if (AutoRetryDelay >= 0.0) {
			abc::ConsoleWarn("[AbcUi] auto retry budget exhausted (%d attempts)\n", AutoRetryCount);
			AutoRetryDelay = -1.0;
		}
		return;
	}
	if (AutoRetryDelay < 0.0) {
		AutoRetryDelay = kAutoRetryDelay;  // arm the next attempt
		abc::ConsolePrint("[AbcUi] auto retry armed: %d/%d in %.0fs\n",
			AutoRetryCount + 1, kAutoRetryMax, kAutoRetryDelay);
		return;
	}
	AutoRetryDelay -= LastFrameTime;
	if (AutoRetryDelay > 0.0)
		return;
	AutoRetryDelay = -1.0;
	++AutoRetryCount;
	abc::ConsolePrint("[AbcUi] auto retry %d/%d\n", AutoRetryCount, kAutoRetryMax);
	retryServerAttempt();
}

void GameUiModule::handleConnectionAction(ActionKind Kind) {
	if (Kind == ActionKind::kDirectConnect)
		Controller.dispatch(MenuEvent{EventKind::kDirectConnectToggle});
	else if (Kind == ActionKind::kLoadingCancel) {
		// The proven termination action (vanilla Cancel). The model latch
		// is the UI-edge guard (exactly one request per attempt); the
		// controller phase gate and the backend's generation latch are
		// the second and third guards behind it.
		const uint32_t live = LoadingModelState.generation();
		if (live != 0 && !LoadingModelState.requestCancel(live))
			return;  // already latched
		Controller.dispatch(MenuEvent{EventKind::kConnectCancel});
	}
	else if (Kind == ActionKind::kErrorRetry) {
		// A manual retry starts a fresh auto-retry chain.
		AutoRetryCount = 0;
		AutoRetryDelay = -1.0;
		if (!retryServerAttempt())
			return;  // nothing to retry: the overlay stays honest
		return;
	}
	else if (Kind == ActionKind::kErrorReturn) {
		AutoRetryCount = 0;
		AutoRetryDelay = -1.0;
		Controller.dispatch(MenuEvent{EventKind::kBack});
	}
	else if (Kind == ActionKind::kDirectConnectConnect) {
		if (!DirectConnect)
			return;
		const std::string address = DirectConnect->value();
		std::string host;
		int port = 0;
		if (!services::loading::LoadingModel::parseAddress(address.c_str(), host, port)) {
			DirectConnect->showHint("#Loading_AddressInvalid");
			return;  // stay put: nothing reaches the engine unvalidated
		}
		// A user-initiated attempt starts a fresh auto-retry chain.
		AutoRetryCount = 0;
		AutoRetryDelay = -1.0;
		Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
	}
	else if (Kind == ActionKind::kDirectConnectCancel)
		Controller.dispatch(MenuEvent{EventKind::kBack});
	else if (Kind == ActionKind::kPasswordConnect) {
		if (!Password)
			return;
		const std::string password = Password->value();
		if (password.empty())
			return;  // empty submissions are not typed intents
		Controller.dispatch(gameui::menu::makePasswordSubmitted(password.c_str()));
	}
	else if (Kind == ActionKind::kPasswordCancel)
		// Vanilla parity: closing the password dialog does not kill the
		// attempt (its backend Close only destroys the dialog). The
		// loading page stays; its Cancel is the termination path.
		Controller.dispatch(MenuEvent{EventKind::kBack});
	finishDispatch();
}
}
