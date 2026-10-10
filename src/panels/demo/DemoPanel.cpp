// panels::DemoPanel implementation - see DemoPanel.h.
#include "DemoPanel.h"

#include <abc/Console.h>

namespace panels {

DemoPanel::DemoPanel()
	: HudPanel("demo", "abcui/rml/demo.rml") {}

void DemoPanel::onDocumentReady() {
	// The demo document shows immediately on load - it is the Phase 3
	// acceptance evidence, and PanelManager's first visible-panel smoke test.
	show();
	abc::ConsolePrint("[AbcUi] demo panel ready\n");
}

}
