// Extracted from GameUiModule.cpp (UTF-8). Responsibility: settings/VideoFlow.
#include "modules/gameui/GameUiModule.h"
#include <abc/Console.h>
#include "modules/gameui/engine/VideoBindings.h"
namespace gameui {
using namespace navigation;
using namespace engine;
void GameUiModule::resolveVideoRecovery() {
	const auto outcome = VideoModel.resolveRecovery();
	switch (outcome) {
	case services::settings::VideoModeModel::RecoveryResult::kPending:
		abc::ConsoleWarn("[AbcUi] display change awaiting confirmation (%s)\n",
			services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
		break;
	case services::settings::VideoModeModel::RecoveryResult::kAutoReverted:
		abc::ConsoleWarn("[AbcUi] unresolved display change restored to %s\n",
			services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
		break;
	case services::settings::VideoModeModel::RecoveryResult::kAccepted:
		// Reserved legacy value; the model never infers confirmation from age.
		break;
	case services::settings::VideoModeModel::RecoveryResult::kDeferred:
		abc::ConsoleWarn("[AbcUi] display-change record kept (recovery query/write pending)\n");
		break;
	case services::settings::VideoModeModel::RecoveryResult::kNoRecord:
		break;
	}
	if (VideoModel.awaitingConfirm())
		DeferredVideoConfirmRaise = true; // frame drain raises it (restart-safe)
}


void GameUiModule::applyVideoChange() {
	if (!VideoModel.available() || !VideoModel.hasStaged() || VideoModel.awaitingConfirm())
		return;
	if (!VideoModel.apply()) {
		abc::ConsoleWarn("[AbcUi] display switch refused (record or command write failed)\n");
		if (VideoModel.awaitingConfirm())
			DeferredVideoConfirmRaise = true;
		if (Settings)
			Settings->render();
		return;
	}
	// The restart is queued in the same transaction (deferred thunk) and
	// the confirmation raise rides the frame drain: never from here.
	DeferredVideoConfirmRaise = true;
	if (Settings)
		Settings->render();
}


void GameUiModule::raiseVideoConfirmation() {
	if (!VideoConfirm || !Context)
		return;
	// The takeover rule: the confirmation outranks everything else in
	// the menu (it is the only way back from a bad mode). If the menu
	// was not up, raise it through the bridge - the takeover keeps the
	// original UI suppressed.
	if (!MenuPresented)
		services::nativeUiBridge().requestMenuUp(services::NativeUiOrigin::kOwnUi);
	// The page covers the whole viewport: hide the other documents while
	// it is up (the sync re-shows the right one when it closes).
	if (Shell)
		Shell->hide();
	if (SubStub)
		SubStub->hide();
	if (Console)
		Console->hide();
	if (Settings)
		Settings->hide();
	VideoConfirmPresented = true;
	VideoCountdownShown = VideoModel.remainingSeconds();
	VideoConfirm->render();
	VideoConfirm->show();
}


void GameUiModule::closeVideoConfirmation() {
	if (VideoConfirm)
		VideoConfirm->hide();
	VideoConfirmPresented = false;
	VideoCountdownShown = -1;
	if (MenuPresented)
		syncMenuPresentation();
}


void GameUiModule::syncVideoConfirmPage() {
	if (!VideoConfirm)
		return;
	if (VideoModel.awaitingConfirm()) {
		// Never raise inside a frame that issued engine commands: queue
		// it for the frame drain (see DeferredVideoConfirmRaise).
		if (!VideoConfirmPresented || !VideoConfirm->isVisible())
			DeferredVideoConfirmRaise = true;
	} else if (VideoConfirmPresented) {
		closeVideoConfirmation();
	}
}


void GameUiModule::tickVideoTimeout() {
	if (!VideoConfirm || !VideoConfirmPresented)
		return;
	const int seconds = VideoModel.remainingSeconds();
	if (seconds == VideoCountdownShown)
		return;
	VideoCountdownShown = seconds;
	VideoConfirm->render();
}

void GameUiModule::handleVideoAction(ActionKind Kind) {
	if (Kind == ActionKind::kVideoApply) {
		applyVideoChange();
		return;
	}
	else if (Kind == ActionKind::kVideoKeep) {
		// Keep the confirmed mode; a refused confirmation stays put (the
		// page re-renders the current truth).
		if (VideoModel.confirm()) {
			abc::ConsolePrint("[AbcUi] display change confirmed (%s)\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			syncVideoConfirmPage();
		} else if (VideoConfirm) {
			VideoConfirm->render();
		}
		return;
	}
	else if (Kind == ActionKind::kVideoRevert) {
		// Restore the previous mode; a refused write keeps the dialog up
		// (the deadline retries every second, the record survives).
		if (VideoModel.revert()) {
			abc::ConsolePrint("[AbcUi] display change reverted to %s\n",
				services::settings::VideoModeModel::describe(VideoModel.actual()).c_str());
			syncVideoConfirmPage();
		} else if (VideoConfirm) {
			VideoConfirm->render();
		}
		return;
	}
}

}
