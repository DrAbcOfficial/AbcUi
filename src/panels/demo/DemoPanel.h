#pragma once

// panels::DemoPanel - Phase 3 acceptance vehicle (plan 05 §9):
// one document demonstrating Chinese text, an image and scissor clipping,
// driven through the full chain (fonts -> DOM -> AbcRenderInterface ->
// IMetaRenderer). Also the input-bridge reachability probe: key/mouse events
// landing on this document log to the console for the acceptance driver.
// Not a real HUD panel; batch A panels supersede it.

#include <RmlUi/Core/EventListener.h>

#include "../../framework/HudPanel.h"

namespace panels {

class DemoPanel : public framework::HudPanel, public Rml::EventListener {
public:
	DemoPanel();

protected:
	void onDocumentReady() override;

	// Rml::EventListener - logs reachability evidence per event type.
	void ProcessEvent(Rml::Event& event) override;

private:
	void attach(const char* eventType);
};

}
