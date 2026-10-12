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

// Nearest ancestor (or self) carrying a data-action; clicks delegated on the
// document resolve their intent here, so clicks landing on inner text still
// find the entry.
Rml::Element* actionNode(Rml::Element* node) {
	for (Rml::Element* it = node; it; it = it->GetParentNode()) {
		if (it->HasAttribute("data-action"))
			return it;
	}
	return nullptr;
}

} // namespace

MainMenuPage::MainMenuPage(const char* documentPath)
	: InteractiveDocument("mainmenu", documentPath), Listener(this) {}

MainMenuPage::~MainMenuPage() {
	// Rml calls OnDetach: the member listener must still be alive here.
	close();
}

void MainMenuPage::onDocumentReady() {
	// One click binding for the whole document (bubbling); focus/blur do not
	// bubble (RmlUi event spec), so every data-label element carries its own
	// pair and the status line follows whichever control holds focus.
	if (Rml::ElementDocument* doc = document())
		addListener(doc, "click", &Listener);
	if (Rml::Element* root = document())
		attachFocusBindings(root);
	// The shipped shell starts on the no-map section; a section switch
	// through showSection re-renders anyway.
	if (Rml::Element* main = document()->GetElementById("page-main")) {
		if (Rml::Element* pause = document()->GetElementById("page-pause"))
			pause->SetProperty("display", "none");
	}
}

void MainMenuPage::showSection(Section section, bool focusDefault) {
	VisibleSection = section;
	if (Rml::Element* main = document() ? document()->GetElementById("page-main") : nullptr)
		main->SetProperty("display", section == Section::kMain ? "block" : "none");
	if (Rml::Element* pause = document() ? document()->GetElementById("page-pause") : nullptr)
		pause->SetProperty("display", section == Section::kPause ? "block" : "none");
	if (!focusDefault)
		return;
	// Default focus: the section's first visible entry. Hidden entries
	// (singleplayer hides the browser/create/list row) are skipped through
	// the fact table, not the DOM - an element property only takes effect at
	// the next layout pass, while the sync needs the answer now.
	if (Rml::Element* node = sectionNode(section)) {
		const int count = node->GetNumChildren();
		for (int i = 0; i < count; i++) {
			Rml::Element* child = node->GetChild(i);
			if (!child || child->GetTagName() != "button" || !child->HasAttribute("data-action"))
				continue;
			const auto fact = EntryVisible.find(child->GetAttribute<Rml::String>("data-action", ""));
			if (fact != EntryVisible.end() && !fact->second)
				continue;
			child->Focus();
			return;
		}
	}
}

void MainMenuPage::setEntryVisible(const char* action, bool visible) {
	if (!action)
		return;
	EntryVisible[action] = visible;  // the fact, readable back immediately
	Rml::Element* root = document();
	if (!root)
		return;
	// Both sections can carry the same action (settings/quit); every match
	// follows the same fact.
	const int count = root->GetNumChildren();
	for (int i = 0; i < count; i++)
		setEntryVisibleRecursive(root->GetChild(i), action, visible);
}

void MainMenuPage::handle(Rml::Event& event) {
	const Rml::EventId id = event.GetId();
	if (id == Rml::EventId::Focus) {
		updateFocusStatus(event.GetCurrentElement());
		return;
	}
	if (id == Rml::EventId::Blur) {
		updateFocusStatus(nullptr);
		return;
	}
	// Click (delegated on the document): resolve the action entry, then hand
	// the fixed action name to the sink. A sink can replace itself or destroy
	// this page, so the callable is snapshotted and nothing is touched on the
	// page afterwards.
	Rml::Element* entry = actionNode(event.GetTargetElement());
	if (!entry)
		return;
	const Rml::String action = entry->GetAttribute<Rml::String>("data-action", "");
	if (action.empty())
		return;
	const ActionSink Action = Sink;
	if (Action)
		Action(action.c_str());
}

void MainMenuPage::updateFocusStatus(Rml::Element* focused) {
	Rml::Element* status = document() ? document()->GetElementById("focus-status") : nullptr;
	if (!status)
		return;
	std::string label;
	if (focused && focused->HasAttribute("data-label"))
		label = resolveLabel(focused->GetAttribute<Rml::String>("data-label", ""));
	else
		label = resolveLabel("#MainMenu_FocusNone");
	status->SetInnerRML(framework::TextUtil::escapeRml(label));
}

void MainMenuPage::attachFocusBindings(Rml::Element* node) {
	if (!node)
		return;
	if (node->HasAttribute("data-label")) {
		addListener(node, "focus", &Listener);
		addListener(node, "blur", &Listener);
	}
	const int count = node->GetNumChildren();
	for (int i = 0; i < count; i++)
		attachFocusBindings(node->GetChild(i));
}

void MainMenuPage::setEntryVisibleRecursive(Rml::Element* node, const char* action, bool visible) {
	if (!node)
		return;
	if (node->HasAttribute("data-action")
		&& node->GetAttribute<Rml::String>("data-action", "") == action)
		node->SetProperty("display", visible ? "block" : "none");
	const int count = node->GetNumChildren();
	for (int i = 0; i < count; i++)
		setEntryVisibleRecursive(node->GetChild(i), action, visible);
}

Rml::Element* MainMenuPage::sectionNode(Section section) {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return nullptr;
	return doc->GetElementById(section == Section::kMain ? "page-main" : "page-pause");
}

void MainMenuPage::PageListener::ProcessEvent(Rml::Event& event) {
	Owner->handle(event);
}

}
