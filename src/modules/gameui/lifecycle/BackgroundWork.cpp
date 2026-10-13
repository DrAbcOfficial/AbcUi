// Extracted from GameUiModule.cpp (UTF-8). Responsibility: lifecycle/Frame.
#include "modules/gameui/GameUiModule.h"
#include <abc/CvarCmd.h>
#include "modules/gameui/engine/VideoBindings.h"
namespace gameui {
using namespace engine;
void GameUiModule::pumpBackgroundWork() {
	// T21: degraded-page retry requests first (a click marks, this beat
	// acts); cheap when nothing is degraded.
	serviceEmergencyPages();
	// T14: the engine's loading stream is drained first and independently
	// of menu presentation - the controller transitions (attempt start,
	// loading, success/failure/password) must not wait for Rml readiness,
	// and the drain is the ONLY consumer of the copied feed.
	drainLoadingFeed();
	// T17: the server-query drain runs every frame on this (main) thread
	// and independently of menu presentation. Provider callbacks arrive
	// from the engine's SteamAPI_RunCallbacks pump and land in the
	// provider's bounded queue; this is the only consumer, and the
	// query deadline is enforced here (bounded per-frame work). The
	// status-edge log rides the same beat.
	ServerBrowser.Query.pump();
	logServerQueryChange();
	// T18: the browser page's projection beat (sync + bounded re-render on
	// change). Runs on the presented latch like the other pages.
	if (MenuPresented && ServerBrowser.Presented && Browser)
		Browser->tick();
	// T20: the player list's frame beat. The roster and every muted state
	// are read through the backend per frame while the page is up (the
	// shipped dialog refreshed on a timer; a read-through keeps the
	// displayed mute labels honest with zero cache). render() no-ops when
	// the model's revision did not move.
	if (MenuPresented && PlayerListPresented && PlayerListPage_) {
		if (PlayerList.refresh())
			PlayerListPage_->render();
	}
	// T12: drain the deferred video restart FIRST and independently of
	// menu presentation - the transaction owns it, not the page (see the
	// DeferredVideoRestart note above).
	const bool restartDrained = DeferredVideoRestart;
	if (DeferredVideoRestart) {
		DeferredVideoRestart = false;
		abc::EngineClientCmd("_restart\n");
	}
	// The queued confirmation raise never shares a frame with a restart
	// drain (see the DeferredVideoConfirmRaise note): one frame apart.
	if (!restartDrained && DeferredVideoConfirmRaise && Context && VideoConfirm) {
		DeferredVideoConfirmRaise = false;
		raiseVideoConfirmation();
	}
	// T12 confirmation beat: the monotonic deadline runs on real time.
	// The tick fires the auto-revert even when the window is hidden or
	// unfocused - the deadline never extends. A revert/auto-resolve
	// re-projects the dialog (or closes it) once.
	if (VideoModel.awaitingConfirm()) {
		const bool reverted = VideoModel.tick();
		tickVideoTimeout();
		if (reverted || !VideoModel.awaitingConfirm())
			syncVideoConfirmPage();
		else if (VideoConfirm)
			VideoConfirm->render();
	}
}
}
