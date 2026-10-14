// Debug-only navigation commands; extracted without changing command names.
#ifndef NDEBUG
#include <metahook.h>

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

// abc_ui_menu_model [<attribute> <value...>] - live tuning of the character
// card's preview view (camera, fov, animation) while the menu is up; with no
// arguments it dumps the current view. Debug-only calibration seam.
void DebugCommands::menuModelCommand() {
	GameUiModule* module = GameUiModule::Instance;
	if (!module || !module->Shell || !module->Context) {
		abc::ConsoleWarn("[AbcUi] menu not available\n");
		return;
	}
	Rml::ElementDocument* document = module->Shell->document();
	Rml::Element* model = document ? document->GetElementById("player-model") : nullptr;
	if (!model) {
		abc::ConsoleWarn("[AbcUi] player card model element not found\n");
		return;
	}
	// argv(1) = attribute, argv(2..) = value (no subcommand level).
	if (gEngfuncs.Cmd_Argc() >= 3) {
		std::string value = gEngfuncs.Cmd_Argv(2);
		for (int i = 3; i < gEngfuncs.Cmd_Argc(); ++i)
			value += std::string(" ") + gEngfuncs.Cmd_Argv(i);
		model->SetAttribute(gEngfuncs.Cmd_Argv(1), value);
		abc::ConsolePrint("[AbcUi] menu model %s = %s\n", gEngfuncs.Cmd_Argv(1), value.c_str());
		return;
	}
	for (const auto& [name, attr] : model->GetAttributes()) {
		if (name == "src" || name == "id" || name == "class" || name == "style")
			continue;
		abc::ConsolePrint("[AbcUi] menu model %s = %s\n", name.c_str(), attr.Get<Rml::String>().c_str());
	}
	abc::ConsolePrint("[AbcUi] menu model box=%.1fx%.1f abs=(%.1f,%.1f)\n", model->GetOffsetWidth(),
		model->GetOffsetHeight(), model->GetAbsoluteLeft(), model->GetAbsoluteTop());
}

}
#endif
