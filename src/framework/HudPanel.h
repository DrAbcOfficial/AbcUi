#pragma once

// framework::HudPanel - panel base class. Plan 05 §4.2.
//
// A panel owns one Rml document plus the controller code that feeds it.
// Subclasses load their RML (linking Theme::kBaseSheetPath first), refresh
// DOM from data in onData, and toggle visibility through show/hide (which
// maps to the document's display property so hidden panels cost no layout).
// No data-binding library; controllers use SetInnerRML/SetAttribute
// directly (plan 05 §4.2).

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string>

namespace framework {

class HudPanel {
public:
	HudPanel(const char* name, const char* documentPath);
	virtual ~HudPanel();

	// Non-copyable: panels are registered once and owned by PanelManager.
	HudPanel(const HudPanel&) = delete;
	HudPanel& operator=(const HudPanel&) = delete;

	// Loads the document into the context; called by PanelManager::initAll.
	bool loadDocument(Rml::Context* context);

	void show();
	void hide();
	bool isVisible() const;

	const std::string& getName() const { return Name; }
	Rml::ElementDocument* getDocument() const { return Document; }

	// Data->DOM refresh; PanelManager::onFrame forwards the pump.
	virtual void onData(double frameTime) { (void)frameTime; }

protected:
	// Post-load hook: query elements, apply initial state.
	virtual void onDocumentReady() {}

	const std::string Name;
	const std::string DocumentPath;
	Rml::ElementDocument* Document = nullptr;
};

}
