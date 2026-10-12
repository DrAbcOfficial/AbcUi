// panels::settings implementation - see SettingsPage.h.
#include "SettingsPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

#include <abc/Format.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/settings/SettingsModel.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace panels {
namespace settings {

namespace {

// Fixed page-internal action names (data-action). Row actions carry the row
// key in data-row - a key from the production description table, never a
// user string.
constexpr const char* kActionApply = "apply";
constexpr const char* kActionReset = "reset-defaults";
constexpr const char* kActionResetYes = "reset-yes";
constexpr const char* kActionResetNo = "reset-no";
constexpr const char* kActionReturn = "return";
constexpr const char* kActionLeaveDiscard = "leave-discard";
constexpr const char* kActionLeaveStay = "leave-stay";
constexpr const char* kActionToggle = "toggle";
constexpr const char* kActionAdopt = "adopt";

constexpr const char* kLayerReset = "reset-confirm";
constexpr const char* kLayerLeave = "leave-confirm";

Rml::Element* actionNode(Rml::Element* node) {
	for (Rml::Element* it = node; it; it = it->GetParentNode()) {
		if (it->HasAttribute("data-action"))
			return it;
	}
	return nullptr;
}

std::string translated(const char* token) {
	// "#token" -> lang table; an unknown token degrades to the raw token
	// (the documented Localization fallback), still escaped by the caller.
	std::string out;
	if (token && *token == '#') {
		if (!framework::Localization::translate(out, token + 1))
			out = token;
	} else if (token) {
		out = token;
	}
	return out;
}

std::string numberText(float value) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
	return buffer;
}

bool isOn(float value) {
	return value > 0.5f;
}

}

SettingsPage::SettingsPage()
	: InteractiveDocument("menusettings", "abcui/rml/settings.rml"), Listener(this) {}

SettingsPage::~SettingsPage() {
	close();
}

void SettingsPage::onDocumentReady() {
	if (Rml::ElementDocument* doc = document()) {
		addListener(doc, "click", &Listener);
		if (Rml::Element* rows = doc->GetElementById("rows"))
			addListener(rows, "change", &Listener);
	}
	buildRows();
	render();
}

void SettingsPage::buildRows() {
	if (!Model || !document())
		return;
	Rml::Element* rows = document()->GetElementById("rows");
	if (!rows)
		return;
	// One skeleton block per description-table row; row DOM ids derive from
	// the cvar names ([a-z_], safe as ids). Delegated listeners need no
	// per-element bindings here.
	std::string rml;
	for (const auto& view : Model->rows()) {
		const bool isBool = view.row->Kind == services::settings::SettingKind::kBool;
		rml += "<div class=\"row\" id=\"row-";
		rml += view.row->Cvar;
		rml += "\"><div class=\"row-label\"></div>";
		if (isBool)
			rml += "<button class=\"row-toggle\" data-action=\"toggle\" data-row=\"";
		else
			rml += "<input class=\"row-input\" type=\"text\" data-row=\"";
		rml += view.row->Cvar;
		rml += "\"/>";
		rml += "<div class=\"row-status\"></div></div>";
	}
	rows->SetInnerRML(rml);
}

void SettingsPage::render(const char* skipInputRow) {
	if (!Model || !document())
		return;
	for (const auto& view : Model->rows())
		renderRow(view, skipInputRow && std::string(view.row->Cvar) == skipInputRow);
	setStatusSummary();
}

void SettingsPage::renderRow(const services::settings::RowView& view, bool skipInput) {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	Rml::Element* rowNode = doc->GetElementById((std::string("row-") + view.row->Cvar).c_str());
	if (!rowNode)
		return;
	if (Rml::Element* label = rowNode->QuerySelector(".row-label"))
		label->SetInnerRML(framework::TextUtil::escapeRml(translated(view.row->LabelToken)));

	const bool isBool = view.row->Kind == services::settings::SettingKind::kBool;
	Rml::Element* control = rowNode->QuerySelector(isBool ? ".row-toggle" : ".row-input");
	if (!control)
		return;
	control->SetClass("row-missing", view.missing);

	if (view.missing) {
		// Unavailable: fail-closed presentation, no editable control path.
		if (Rml::ElementFormControl* form = rmlui_dynamic_cast<Rml::ElementFormControl*>(control))
			form->SetDisabled(true);
		return;
	}

	const float shownValue = view.state == services::settings::RowState::kInvalid
		? view.engineValue
		: view.draftValue;
	if (isBool) {
		control->SetInnerRML(framework::TextUtil::escapeRml(
			translated(isOn(shownValue) ? "#Settings_On" : "#Settings_Off")));
	} else if (!skipInput) {
		// The committed text is the projection; a programmatic SetValue does
		// not dispatch change (no feedback loop through the model). The
		// origin field of a change commit is skipped entirely so its caret
		// and IME state survive the status refresh.
		if (Rml::ElementFormControl* form = rmlui_dynamic_cast<Rml::ElementFormControl*>(control))
			form->SetValue(view.draftText.c_str());
	}

	// Row status: domain facts plus the pending-state suffix.
	std::string status;
	if (view.row->Kind == services::settings::SettingKind::kFloat)
		status = abc::Format("{0}-{1} {2} {3}", numberText(view.row->Min), numberText(view.row->Max),
			translated("#Settings_Default"), numberText(view.row->DefaultValue));
	else
		status = translated("#Settings_BoolDomain");
	switch (view.state) {
	case services::settings::RowState::kDirty:
		status += " | " + translated("#Settings_Dirty");
		break;
	case services::settings::RowState::kInvalid:
		// The typed text is user input: it rides the status string and the
		// single escapeRml below makes it inert (no inner escaping here -
		// that would double-encode).
		status += " | " + translated("#Settings_Invalid") + " (" + view.draftText + ")";
		break;
	case services::settings::RowState::kConflict:
		status += " | " + abc::Format("{0} {1}", translated("#Settings_Conflict"),
			numberText(view.engineValue));
		break;
	case services::settings::RowState::kApplyFailed:
		status += " | " + translated("#Settings_ApplyFailed");
		break;
	case services::settings::RowState::kClean:
	case services::settings::RowState::kMissing:
		break;
	}
	if (Rml::Element* statusNode = rowNode->QuerySelector(".row-status"))
		statusNode->SetInnerRML(framework::TextUtil::escapeRml(status));
}

void SettingsPage::setStatusSummary() {
	Rml::ElementDocument* doc = document();
	if (!doc || !Model)
		return;
	Rml::Element* status = doc->GetElementById("settings-status");
	if (!status)
		return;
	std::string text;
	if (!Model->isDirty()) {
		text = translated("#Settings_StatusClean");
	} else {
		text = translated("#Settings_StatusDirty");
		if (Model->hasConflicts())
			text += " | " + translated("#Settings_StatusConflict");
	}
	status->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void SettingsPage::applyAndReport() {
	if (!Model || !document())
		return;
	const auto results = Model->apply();
	if (Rml::Element* status = document()->GetElementById("settings-status")) {
		std::string text;
		if (results.empty()) {
			text = translated("#Settings_StatusClean");
		} else {
			int applied = 0, failed = 0;
			for (const auto& result : results) {
				if (result.outcome == services::settings::ApplyOutcome::kApplied)
					applied++;
				else
					failed++;
			}
			text = failed == 0
				? translated("#Settings_Applied")
				: abc::Format("{0}/{1} {2}", std::to_string(applied), std::to_string(applied + failed),
					translated("#Settings_ApplyPartial"));
		}
		status->SetInnerRML(framework::TextUtil::escapeRml(text));
	}
	render();
}

bool SettingsPage::isDirty() const {
	return Model && Model->isDirty();
}

void SettingsPage::requestLeave() {
	if (!Model)
		return;
	if (!Model->isDirty()) {
		const ActionSink action = Sink;
		if (action)
			action(kActionReturn);
		return;
	}
	if (ActiveLayer == kLayerLeave) {
		hideLayers(); // second request: dismissing the prompt is "stay"
		return;
	}
	showLayer(kLayerLeave);
}

void SettingsPage::showLayer(const char* id) {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	hideLayers();
	if (Rml::Element* layer = doc->GetElementById(id)) {
		layer->SetProperty("display", "block");
		ActiveLayer = id;
	}
	// autofocus only rides the document Show; a layer opened later takes its
	// safe default explicitly (cancel/stay, never the destructive entry).
	const char* safe = (std::string(id) == kLayerReset) ? "reset-no" : "leave-stay";
	if (Rml::Element* focus = doc->GetElementById(safe))
		focus->Focus();
}

void SettingsPage::hideLayers() {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	for (const char* id : {kLayerReset, kLayerLeave}) {
		if (Rml::Element* layer = doc->GetElementById(id))
			layer->SetProperty("display", "none");
	}
	ActiveLayer.clear();
}

void SettingsPage::refocusDefault() {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	if (!ActiveLayer.empty()) {
		const char* safe = (ActiveLayer == kLayerReset) ? "reset-no" : "leave-stay";
		if (Rml::Element* focus = doc->GetElementById(safe))
			focus->Focus();
		return;
	}
	if (Rml::Element* focus = doc->GetElementById("settings-apply"))
		focus->Focus();
}

void SettingsPage::handleClick(Rml::Event& event) {
	Rml::Element* entry = actionNode(event.GetTargetElement());
	if (!entry)
		return;
	const Rml::String action = entry->GetAttribute<Rml::String>("data-action", "");
	const Rml::String rowKey = entry->GetAttribute<Rml::String>("data-row", "");
	const std::string which = action;

	if (which == kActionApply) {
		applyAndReport();
	} else if (which == kActionReset) {
		showLayer(kLayerReset); // restore-defaults always confirms first
	} else if (which == kActionResetYes) {
		if (Model)
			Model->resetDefaults();
		hideLayers();
		render();
	} else if (which == kActionResetNo) {
		hideLayers();
	} else if (which == kActionLeaveDiscard) {
		// The discard IS the cancel: no write, drafts gone, then return.
		if (Model)
			Model->cancel();
		hideLayers();
		render();
		const ActionSink action2 = Sink;
		if (action2)
			action2(kActionReturn);
	} else if (which == kActionLeaveStay) {
		hideLayers();
	} else if (which == kActionToggle && !rowKey.empty()) {
		if (Model) {
			const auto* view = Model->find(rowKey.c_str());
			if (view && !view->missing)
				Model->setDraftBool(rowKey.c_str(), !isOn(view->state
								== services::settings::RowState::kInvalid
							? view->engineValue
							: view->draftValue));
		}
		render();
	} else if (which == kActionAdopt && !rowKey.empty()) {
		if (Model)
			Model->adoptEngineValue(rowKey.c_str());
		render();
	} else if (which == kActionReturn) {
		requestLeave(); // clean: reports through the sink; dirty: prompts
	}
	// Unknown actions are dropped: the page only binds its own names.
}

void SettingsPage::handleChange(Rml::Event& event) {
	Rml::Element* target = event.GetTargetElement();
	if (!target || !Model)
		return;
	Rml::Element* entry = target;
	while (entry && !entry->HasAttribute("data-row"))
		entry = entry->GetParentNode();
	if (!entry)
		return;
	const Rml::String rowKey = entry->GetAttribute<Rml::String>("data-row", "");
	if (rowKey.empty())
		return;
	const Rml::String text = event.GetParameter<Rml::String>("value", Rml::String());
	Model->setDraftText(rowKey.c_str(), text.c_str());
	render(rowKey.c_str());
}

void SettingsPage::PageListener::ProcessEvent(Rml::Event& event) {
	if (event.GetType() == "change")
		Owner->handleChange(event);
	else
		Owner->handleClick(event);
}

}
}
