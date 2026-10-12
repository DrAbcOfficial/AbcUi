// framework::HudPanel implementation - see HudPanel.h.
#include "HudPanel.h"
#include "../data/HudState.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/Console.h>

namespace framework {

HudPanel::HudPanel(const char* name, const char* documentPath)
	: Name(name), DocumentPath(documentPath) {}

HudPanel::~HudPanel() {
	for (auto& cleanup : Cleanups)
		cleanup();
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
	// Passive HUD documents must not intercept scoreboard/MOTD hit testing.
	if (Name != "scoreboard" && Name != "motd") Document->SetProperty("pointer-events", "none");
	// Start hidden; panels opt into visibility via show() (data-driven).
	Document->Hide();
	onDocumentReady();
	return true;
}

void HudPanel::show() {
	// Idempotent: RmlUi's Show() defaults to FocusFlag::Auto, so re-showing a
	// visible document would silently steal the context focus (key input of
	// whatever document the user is interacting with, e.g. an open TextMenu).
	if (hudstate::isHudHidden(hudstate::kHideAll) && Name != "scoreboard" && Name != "motd" && Name != "textmenu" && Name != "vote") return;
	if (Document && !Document->IsVisible())
		Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void HudPanel::hide() {
	if (Document)
		Document->Hide();
}

bool HudPanel::isVisible() const {
	return Document && Document->IsVisible();
}

void HudPanel::addCleanup(std::function<void()> fn) {
	Cleanups.push_back(std::move(fn));
}

Rml::Element* HudPanel::element(const char* id) {
	Rml::Element* found = Document ? Document->GetElementById(id) : nullptr;
	if (!found)
		abc::ConsoleWarn("[AbcUi] panel \"%s\": element \"%s\" missing\n", Name.c_str(), id);
	return found;
}

}
