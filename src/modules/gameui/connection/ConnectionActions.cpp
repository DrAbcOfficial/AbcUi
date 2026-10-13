// Typed connection intents; no command parsing or UI visibility policy.
#include "modules/gameui/GameUiModule.h"
#include <string>
namespace gameui {
using namespace navigation;

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
		// A NEW attempt generation from the same target (kept by the
		// model/request across the failure) - never a reuse of the dead
		// one. T19: a failed LOCAL attempt replays its kept request
		// through the same typed path (the launch re-runs from scratch;
		// the phase gate keeps the double-retry honest).
		if (PendingCreateGameValid && LastAttemptWasLocal) {
			Controller.dispatch(
				gameui::menu::makeCreateGameRequest(PendingCreateGame.Map.c_str()));
			finishDispatch();
			return;
		}
		const std::string address = LoadingModelState.address();
		if (address.empty())
			return;  // nothing to retry: the overlay stays honest
		Controller.dispatch(gameui::menu::makeConnectRequest(address.c_str()));
	}
	else if (Kind == ActionKind::kErrorReturn) {
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
