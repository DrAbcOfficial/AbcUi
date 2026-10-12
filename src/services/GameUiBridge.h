#pragma once

// services::GameUiBridge - the typed backend seam between the pure menu state
// machine (gameui::menu, T04) and the engine/GameUI integration layer that a
// later task (T08/T09+) implements. T04 freezes the type contract only.
//
// Direction of data:
//   OUT  menu decisions      - IGameUiBackend below, injected into
//                              gameui::menu::MenuController; the state machine
//                              never reads VGUI pointers and never issues an
//                              engine command itself (T04 step 4).
//   IN   backend/native state- gameui::menu::MenuEvent value objects fed to
//                              MenuController::dispatch. Loading events are
//                              generation-tagged, phase events session-tagged,
//                              native hook events carry origin + sequence so
//                              dual-fired callbacks dedupe (hook-contract.md
//                              T5/T6). All strings arrive copied.
//
// Hard rules the implementations must keep once this seam is wired:
//   - User-provided strings (server address, password, error text) travel as
//     data payloads only; they must never become console command names
//     (T04 step 2). The original command chains (connect, password %s +
//     retry, disconnect, quit) are rebuilt INSIDE a backend implementation,
//     one method per intent.
//   - Single-player pause is NOT part of this contract: whether the original
//     setpause/unpause behaviour is needed awaits backend proof (T04 step 5,
//     hook-contract.md §4); resumeGame() covers the menu-side close only.
//   - Hook installation and chain ownership stay with the spine/integration
//     layer (coding rule 5); this header never names a hook.

#include <cstdint>

namespace services {

// Executor of the typed menu actions (T04 step 2). Implementations copy any
// string they keep past the call; calls happen synchronously inside
// MenuController::dispatch on the main thread.
class IGameUiBackend {
public:
	virtual ~IGameUiBackend() = default;

	// Start connecting to `address`. `generation` is the state machine's
	// attempt id; later cancelConnect/submitPassword and the backend's own
	// loading events must carry the same generation so stale results of a
	// superseded attempt are dropped.
	virtual void connect(uint32_t generation, const char* address) = 0;
	// Give up on the live attempt (loading cancel path).
	virtual void cancelConnect(uint32_t generation) = 0;
	// Password retry path of the live attempt (original password %s + retry
	// chain; the real chain is T14/T17 wiring).
	virtual void submitPassword(uint32_t generation, const char* password) = 0;
	// Leave the current game (original Disconnect command backend).
	virtual void disconnectFromGame() = 0;
	// Confirmed quit (the confirm dialog is menu-side state; only the
	// confirmed decision reaches here, original QuitNoConfirm branch).
	virtual void quitGame() = 0;
	// Close the menu and hand input back to the game (original ResumeGame /
	// HideGameUI decision made menu-side). Issued only when the state machine
	// can actually close (in-game); never unconditionally.
	virtual void resumeGame() = 0;
};

}
