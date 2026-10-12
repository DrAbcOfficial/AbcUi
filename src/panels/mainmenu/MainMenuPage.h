#pragma once

// panels::mainmenu - the T05 minimal interactive page: a title, one action
// button, a focus-visibility status line, text acceptance fields (single
// line, password, multiline) and close/return.
//
// Business actions never open an original GameUI window (T05 forbidden
// list): every element carrying a data-action attribute hands the action
// name to the injected ActionSink. T05 wires a logging fake; T09 binds the
// real gameui backend to this seam. close/return hide the page themselves
// (a window must be closable on its own) and additionally report through
// the sink so diagnostics can follow the click.
//
// Input reachability: the page is mouse-hit-testable through the context's
// normal input path; keyboard arbitration is T06's router and text/IME
// delivery is T07's rmlui::TextInput (focused editors receive committed
// text, composition and caret keys; the fields demonstrate every case).

#include "../../framework/InteractiveDocument.h"

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace Rml {
class Event;
}

namespace panels::mainmenu {

// Injected business-action seam; invoked on the main thread from click
// handlers. Empty sink = actions are dropped (the page stays functional).
using ActionSink = std::function<void(const char* action)>;

class MainMenuPage final : public framework::InteractiveDocument {
public:
	MainMenuPage();

	void setActionSink(ActionSink sink) { Sink = std::move(sink); }

protected:
	void onDocumentReady() override;

private:
	// The page's single listener instance, attached per element in
	// onDocumentReady. The owner detaches every binding before the document
	// closes (InteractiveDocument contract), so this pointer can never reach
	// a dead DOM; the listener dies with the page, which always closes (or
	// has already closed) the document first.
	class PageListener final : public Rml::EventListener {
	public:
		explicit PageListener(MainMenuPage* owner) : Owner(owner) {}
		void ProcessEvent(Rml::Event& event) override;

	private:
		MainMenuPage* Owner;
	};

	void handle(Rml::Event& event);
	void updateFocusStatus(Rml::Element* focused);

	PageListener Listener;
	ActionSink Sink;
};

}
