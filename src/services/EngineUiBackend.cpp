// services::EngineUiBackend implementation - see EngineUiBackend.h.
#include "EngineUiBackend.h"

#include <abc/Console.h>

namespace services {

void EngineUiBackend::connect(uint32_t generation, const char* address) {
	// T14/T17 wire the loading/query chain; until then the intent is typed
	// and dropped - never a hand-built "connect %s" console string.
	abc::ConsoleWarn("[AbcUi] connect backend not wired yet (T14/T17), dropped gen=%u addr=%s\n",
		generation, address ? address : "");
}

void EngineUiBackend::cancelConnect(uint32_t generation) {
	abc::ConsoleWarn("[AbcUi] connect cancel backend not wired yet (T14), dropped gen=%u\n", generation);
}

void EngineUiBackend::submitPassword(uint32_t generation, const char* password) {
	abc::ConsoleWarn("[AbcUi] password backend not wired yet (T14), dropped gen=%u\n", generation);
	(void)password;  // never logged: the retry chain is the only consumer (T14)
}

void EngineUiBackend::disconnectFromGame() {
	// The original Disconnect menu command, verbatim (GameUI.dll 0x1000cc30).
	clientCmd("disconnect\n");
}

void EngineUiBackend::quitGame() {
	if (QuitIssued)
		return;  // one execution, whatever arrives afterwards (T09 step 3)
	QuitIssued = true;
	// The confirmed branch of the original Quit flow (QuitNoConfirm).
	clientCmd("quit\n");
}

void EngineUiBackend::resumeGame() {
	// Vanilla ResumeGame = IBaseUI::HideGameUI, never a command. Lower the
	// bridge first: the outer hide re-enters the (suppressed) inner dispatch,
	// which must find the menu already down so no routed-close echo fires.
	if (Seams_.bridge)
		Seams_.bridge->requestMenuDown(NativeUiOrigin::kOwnUi);
	if (Seams_.hideGameUi)
		Seams_.hideGameUi();  // engine root side effects (warn when absent)
	else
		abc::ConsoleWarn("[AbcUi] hideGameUi seam missing - resume skips the engine-side hide\n");
}

void EngineUiBackend::menuShownInGame() {
	// Vanilla ActivateGameUI inner body (0x100061d0), singleplayer only.
	if (singlePlayer())
		clientCmd("setpause\n");
}

void EngineUiBackend::menuHiddenInGame() {
	// Vanilla HideGameUI inner body minus hideconsole (0x10006420; the legacy
	// console it targets stays suppressed until T16 replaces it).
	if (singlePlayer())
		clientCmd("unpause\n");
}

bool EngineUiBackend::singlePlayer() const {
	return Seams_.maxClients && Seams_.maxClients() == 1;
}

void EngineUiBackend::clientCmd(const char* command) {
	if (!Seams_.clientCmd) {
		abc::ConsoleWarn("[AbcUi] engine command seam missing, dropped \"%s\"\n", command);
		return;
	}
	Seams_.clientCmd(command);
}

}
