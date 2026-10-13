// Debug-only console commands; extracted without changing command names.
#ifndef NDEBUG
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
namespace gameui {
void DebugCommands::consoleUiStatusCommand() {
	if (!GameUiModule::Instance) {
		abc::ConsoleWarn("[AbcUi] no module instance\n");
		return;
	}
	GameUiModule& self = *GameUiModule::Instance;
	auto& bridge = services::nativeUiBridge();
	int rows = -1;
	int scrollTop = -1;
	int scrollHeight = -1;
	if (self.Console && self.Console->document()) {
		if (Rml::Element* history = self.Console->document()->GetElementById("console-history")) {
			rows = history->GetNumChildren();
			scrollTop = static_cast<int>(history->GetScrollTop());
			scrollHeight = static_cast<int>(history->GetScrollHeight());
		}
	}
	abc::ConsolePrint(
		"[AbcUi] console-ui: requested=%d visible=%d page=%d menu=%d rows=%d scrollTop=%d scrollHeight=%d\n",
		bridge.consoleRequested() ? 1 : 0, bridge.consoleVisible() ? 1 : 0,
		self.ConsolePresented ? 1 : 0, self.MenuPresented ? 1 : 0, rows, scrollTop, scrollHeight);
}


void DebugCommands::consoleUiToggleCommand() {
	if (!GameUiModule::Instance) {
		abc::ConsoleWarn("[AbcUi] no module instance\n");
		return;
	}
	GameUiModule::Instance->onMenuAction("console");
	consoleUiStatusCommand();
}

}
#endif
