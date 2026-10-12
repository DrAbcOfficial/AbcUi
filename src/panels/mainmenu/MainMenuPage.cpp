// panels::mainmenu implementation - see MainMenuPage.h.
#include "MainMenuPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

namespace panels::mainmenu {

namespace {

// data-label attributes carry "#token" form like text nodes do; attributes
// skip Rml's translate hook, so resolve through the framework table here.
std::string resolveLabel(std::string label) {
	if (!label.empty() && label[0] == '#') {
		std::string out;
		if (framework::Localization::translate(out, label.substr(1)))
			return out;
	}
	return label;
}

} // namespace

MainMenuPage::MainMenuPage()
	: InteractiveDocument("mainmenu", "abcui/rml/mainmenu.rml"), Listener(this) {}

MainMenuPage::~MainMenuPage() {
	// Rml calls OnDetach: the member listener must still be alive here.
	close();
}

void MainMenuPage::onDocumentReady() {
	// focus/blur do not bubble (RmlUi event spec), so each focusable element
	// carries its own pair; click bubbles but stays per element to keep the
	// data-action source unambiguous. T07 extends the set with the text
	// acceptance fields (password, multiline) - the status line follows
	// whichever control holds keyboard focus.
	if (Rml::Element* button = element("action-button")) {
		addListener(button, "click", &Listener);
		addListener(button, "focus", &Listener);
		addListener(button, "blur", &Listener);
	}
	for (const char* id : {"text-field", "password-field", "area-field"}) {
		if (Rml::Element* field = element(id)) {
			addListener(field, "focus", &Listener);
			addListener(field, "blur", &Listener);
		}
	}
	if (Rml::Element* close = element("close-button")) {
		addListener(close, "click", &Listener);
		addListener(close, "focus", &Listener);
		addListener(close, "blur", &Listener);
	}
	if (Rml::Element* back = element("return-button")) {
		addListener(back, "click", &Listener);
		addListener(back, "focus", &Listener);
		addListener(back, "blur", &Listener);
	}
}

void MainMenuPage::handle(Rml::Event& event) {
	// GetCurrentElement: the element this binding hangs on - clicks landing
	// on inner text nodes still resolve to the action-carrying element.
	Rml::Element* current = event.GetCurrentElement();
	if (!current)
		return;

	const Rml::EventId id = event.GetId();
	if (id == Rml::EventId::Focus) {
		updateFocusStatus(current);
		return;
	}
	if (id == Rml::EventId::Blur) {
		updateFocusStatus(nullptr);
		return;
	}

	const std::string action = current->GetAttribute<Rml::String>("data-action", "");
	if (action.empty())
		return;
	// A sink can replace itself or destroy this page. Finish page work first
	// and keep the callable alive independently of its member storage.
	const ActionSink Action = Sink;
	if (action == "close" || action == "return")
		hide();
	if (Action)
		Action(action.c_str());
}

void MainMenuPage::updateFocusStatus(Rml::Element* focused) {
	Rml::Element* status = element("focus-status");
	if (!status)
		return;
	std::string label;
	if (focused)
		label = resolveLabel(focused->GetAttribute<Rml::String>("data-label",
			focused->GetId()));
	else
		label = resolveLabel("#MainMenu_FocusNone");
	status->SetInnerRML(framework::TextUtil::escapeRml(label));
}

void MainMenuPage::PageListener::ProcessEvent(Rml::Event& event) {
	Owner->handle(event);
}

}
