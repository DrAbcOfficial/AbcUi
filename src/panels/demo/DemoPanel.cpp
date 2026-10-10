// panels::DemoPanel implementation - see DemoPanel.h.
#include "DemoPanel.h"

#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

#include <abc/Console.h>

namespace panels {

namespace {
// KI_A..KI_Z are contiguous; render letters as letters for the acceptance
// assertions, everything else as its numeric identifier.
std::string keyName(Rml::Input::KeyIdentifier identifier) {
	if (identifier >= Rml::Input::KI_A && identifier <= Rml::Input::KI_Z)
		return std::string(1, static_cast<char>('A' + (static_cast<int>(identifier) - static_cast<int>(Rml::Input::KI_A))));
	return std::to_string(static_cast<int>(identifier));
}
} // namespace

DemoPanel::DemoPanel()
	: HudPanel("demo", "abcui/rml/demo.rml") {}

void DemoPanel::onDocumentReady() {
	// Reachability probes: these fire through
	// HUD_Key_Event -> KeyMap -> Context::ProcessKeyDown and
	// VGUI cursor poll -> Context::ProcessMouseMove/Down.
	attach("keydown");
	attach("mousemove");
	attach("mousedown");
	// The demo document shows immediately on load - it is the Phase 3
	// acceptance evidence, and PanelManager's first visible-panel smoke test.
	show();
	Document->Focus();
	abc::ConsolePrint("[AbcUi] demo panel ready\n");
}

void DemoPanel::attach(const char* eventType) {
	Document->AddEventListener(eventType, this, true);
}

void DemoPanel::ProcessEvent(Rml::Event& event) {
	const Rml::String& type = event.GetType();
	if (type == "keydown") {
		const auto identifier = static_cast<Rml::Input::KeyIdentifier>(
			event.GetParameter<int>("key_identifier", 0));
		abc::ConsolePrint("[AbcUi] demo key %s\n", keyName(identifier).c_str());
	}
	else if (type == "mousemove") {
		abc::ConsolePrint("[AbcUi] demo mouse %d,%d\n",
			event.GetParameter<int>("mouse_x", -1), event.GetParameter<int>("mouse_y", -1));
	}
	else if (type == "mousedown") {
		abc::ConsolePrint("[AbcUi] demo button %d\n", event.GetParameter<int>("button", -1));
	}
}

}
