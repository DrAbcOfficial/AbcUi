// framework::HudPanel implementation - see HudPanel.h.
#include "HudPanel.h"

#include <abc/Console.h>

namespace framework {

HudPanel::HudPanel(const char* name, const char* documentPath)
	: Name(name), DocumentPath(documentPath) {}

HudPanel::~HudPanel() {
	if (Document)
		Document->Close();
}

bool HudPanel::loadDocument(Rml::Context* context) {
	if (!context || Document)
		return Document != nullptr;
	Document = context->LoadDocument(DocumentPath);
	if (!Document) {
		abc::ConsoleWarn("[AbcUi] panel \"%s\": failed to load %s\n", Name.c_str(), DocumentPath.c_str());
		return false;
	}
	// Start hidden; panels opt into visibility via show() (data-driven).
	Document->Hide();
	onDocumentReady();
	return true;
}

void HudPanel::show() {
	if (Document)
		Document->Show();
}

void HudPanel::hide() {
	if (Document)
		Document->Hide();
}

bool HudPanel::isVisible() const {
	return Document && Document->IsVisible();
}

}
