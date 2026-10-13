// Extracted from GameUiModule.cpp (UTF-8). Responsibility: settings/SettingsFlow.
#include "modules/gameui/GameUiModule.h"
namespace gameui {
void GameUiModule::presentSettingsPage() {
	if (!Settings)
		return;
	if (!SettingsPresented) {
		SettingsModel.snapshot();
		BindModel.snapshot();
		VideoModel.snapshot(); // fresh actual mode + enumerated list
		VoiceModel.snapshot(); // fresh device floats + interface presence
		Settings->render();
		SettingsPresented = true;
	}
	Settings->show();
}

}
