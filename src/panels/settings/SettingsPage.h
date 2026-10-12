#pragma once

// panels::settings - the T10 basic settings page (one interactive document).
//
// The page is a PROJECTION of services::settings::SettingsModel: rows are
// built from the model's description table, every field edit lands in the
// model's draft, and Apply/Cancel/Reset go through the model's transaction.
// Nothing here writes a cvar directly and no game state is inferred from the
// DOM - the gameui module decides visibility and calls render()/requestLeave()
// on its sync, exactly like the main-menu shell.
//
// Page-internal layers (reset confirmation, unsaved-changes prompt) are DOM
// overlays inside this document, not controller overlays: the T04 controller
// table is frozen and these prompts never outlive the page. The module keeps
// owning the ESC/return decision: it calls requestLeave() before navigating
// back, and the page either shows the unsaved prompt (intent consumed) or
// reports the fixed "return" action through the sink.
//
// Rendering rules: every dynamic string reaches the DOM through
// Localization::translate + TextUtil::escapeRml (draft text is user input);
// all layout is dp (the context density ratio scales). Visibility of the
// layers is tracked in page members, never inferred from IsVisible() (a
// display property lands at the next layout frame - T09 focus-guard fact).

#include "../../framework/InteractiveDocument.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Event;
}

namespace services {
namespace settings {
class SettingsModel;
struct RowView;
}
}

namespace panels {
namespace settings {

// Same seam contract as the other menu windows: fixed page-internal action
// names only ("return"), never user strings.
using ActionSink = std::function<void(const char* action)>;

class SettingsPage final : public framework::InteractiveDocument {
public:
	SettingsPage();
	~SettingsPage() override;

	void setActionSink(ActionSink sink) { Sink = std::move(sink); }
	void setModel(services::settings::SettingsModel* model) { Model = model; }

	// Re-projects the whole model into the DOM. Safe while hidden. When
	// `skipInputRow` is non-null, that row's text field keeps its exact
	// editor state (the change-commit path passes the row it came from).
	void render(const char* skipInputRow = nullptr);

	// True while Apply would still change (or has failed to change)
	// something: the module's ESC/return guard asks before discarding.
	bool isDirty() const;

	// Leave guard for the module: clean pages report "return" through the
	// sink; dirty pages show the unsaved-changes layer and consume the
	// intent instead. A second call while the layer is up dismisses it.
	void requestLeave();

	// Re-takes the safe default focus (module's per-frame focus guard): the
	// top-most layer's safe entry, else the Apply button.
	void refocusDefault();

protected:
	void onDocumentReady() override;

private:
	// One delegated listener pair: click on the document (bubbling),
	// change on the rows container (bubbling from the text fields).
	class PageListener final : public Rml::EventListener {
	public:
		explicit PageListener(SettingsPage* owner) : Owner(owner) {}
		void ProcessEvent(Rml::Event& event) override;

	private:
		SettingsPage* Owner;
	};

	void handleClick(Rml::Event& event);
	void handleChange(Rml::Event& event);
	void buildRows();
	void renderRow(const services::settings::RowView& view, bool skipInput);
	void setStatusSummary();
	void applyAndReport();
	void showLayer(const char* id);
	void hideLayers();
	bool layerVisible() const { return !ActiveLayer.empty(); }

	PageListener Listener;
	ActionSink Sink;
	services::settings::SettingsModel* Model = nullptr;
	// Page-internal modal state: "" when no layer is up, else the layer's
	// DOM id ("reset-confirm" / "leave-confirm"). The truth for event
	// routing, kept in lockstep with the display property.
	std::string ActiveLayer;
};

}
}
