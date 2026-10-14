// Debug-only console commands; extracted without changing command names.
#ifndef NDEBUG
#include <metahook.h>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "modules/gameui/GameUiModule.h"
#include "DebugCommands.h"
#include <abc/Console.h>
#include "services/console/ConsoleBackend.h"
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
	// Completion catalog probe (WS6): what the dropdown would show for "sv".
	{
		const auto matches = services::console::consoleBackend().complete("sv", 12);
		abc::ConsolePrint("[AbcUi] console-ui: complete('sv') -> %u matches", static_cast<unsigned>(matches.size()));
		for (std::size_t i = 0; i < matches.size() && i < 12; ++i)
			abc::ConsolePrint(" %s", matches[i].c_str());
		abc::ConsolePrint("\n");
		if (Rml::ElementDocument* document = self.Console ? self.Console->document() : nullptr) {
			Rml::Element* value = document->GetElementById("console-input");
			abc::ConsolePrint("[AbcUi] console-ui: editor value='%s'\n",
				value ? value->GetAttribute<Rml::String>("value", "").c_str() : "<none>");
		}
		if (gEngfuncs.Cmd_Argc() >= 2 && self.Console)
			self.Console->debugType(gEngfuncs.Cmd_Argv(1));
		if (Rml::ElementDocument* document = self.Console ? self.Console->document() : nullptr) {
			if (Rml::Element* list = document->GetElementById("console-suggestions")) {
				abc::ConsolePrint("[AbcUi] console-ui: dropdown rows=%d\n", list->GetNumChildren());
				for (int i = 0; i < list->GetNumChildren() && i < 4; ++i)
					abc::ConsolePrint("[AbcUi] console-ui: row[%d]='%s'\n", i,
						list->GetChild(i)->GetInnerRML().c_str());
			}
		}
	}
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
