// Debug-only navigation commands; extracted without changing command names.
#ifndef NDEBUG
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
namespace gameui {
void DebugCommands::menuPageCommand() {		if (!GameUiModule::Instance || !GameUiModule::Instance->Shell || !GameUiModule::Instance->Context) {
		abc::ConsoleWarn("[AbcUi] menu not available\n");
		return;
	}
	auto& bridge = services::nativeUiBridge();
	if (GameUiModule::Instance->MenuPresented) {
		bridge.requestMenuDown(services::NativeUiOrigin::kOwnUi);
		abc::ConsolePrint("[AbcUi] menu hidden (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
		return;
	}
	// Diagnostic raise: the same sink path the takeover drives (this is
	// the T05/T06 dev seam, not a second truth).
	bridge.requestMenuUp(services::NativeUiOrigin::kOwnUi);
	abc::ConsolePrint("[AbcUi] menu shown (bridge menuUp=%d)\n", bridge.menuUp() ? 1 : 0);
}

}
#endif
