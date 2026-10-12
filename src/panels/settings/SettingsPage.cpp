// panels::settings implementation - see SettingsPage.h.
#include "SettingsPage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

#include <abc/Format.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"
#include "../../services/settings/KeyBindModel.h"
#include "../../services/settings/SettingsModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
// T11 bind-section actions. The bind row key in data-row is the action's
// index in the model table (a plain number, not a command string).
constexpr const char* kActionBindCapture = "bind-capture";
constexpr const char* kActionBindCaptureBegin = "bind-capture-begin"; // to the module
constexpr const char* kActionBindClear = "bind-clear";
constexpr const char* kActionBindRemove = "bind-remove";
constexpr const char* kActionBindReset = "bind-reset-defaults";
constexpr const char* kActionBindResetYes = "bind-reset-yes";
constexpr const char* kActionBindResetNo = "bind-reset-no";
constexpr const char* kActionBindConflictYes = "bind-conflict-yes";
constexpr const char* kActionBindConflictNo = "bind-conflict-no";

constexpr const char* kLayerReset = "reset-confirm";
constexpr const char* kLayerLeave = "leave-confirm";
constexpr const char* kLayerBindConflict = "bind-conflict";
constexpr const char* kLayerBindReset = "bind-reset-confirm";

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
	buildBindRows();
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

void SettingsPage::buildBindRows() {
	if (!BindModel || !document())
		return;
	Rml::Element* rows = document()->GetElementById("bindrows");
	if (!rows)
		return;
	// One skeleton block per kb_act entry: section headers render as plain
	// dividers, actions carry label + key list + bind/clear buttons. The DOM
	// id is the action index (a plain number).
	std::string rml;
	for (const auto& view : BindModel->rows()) {
		if (view.header) {
			rml += "<div class=\"bind-header\"></div>";
			continue;
		}
		rml += "<div class=\"bind-row\" id=\"bindrow-";
		rml += std::to_string(view.actionIndex);
		rml += "\"><div class=\"bind-line\">";
		rml += "<div class=\"bind-label\"></div>";
		rml += "<div class=\"bind-keys\"></div>";
		rml += "<button class=\"bind-btn bind-capture-btn\" data-action=\"bind-capture\" data-row=\"";
		rml += std::to_string(view.actionIndex);
		rml += "\"/>";
		rml += "<button class=\"bind-btn bind-clear-btn\" data-action=\"bind-clear\" data-row=\"";
		rml += std::to_string(view.actionIndex);
		rml += "\"/>";
		rml += "</div>";
		rml += "<div class=\"bind-status\"></div></div>";
	}
	rows->SetInnerRML(rml);
}

void SettingsPage::render(const char* skipInputRow) {
	if (!document())
		return;
	if (Model) {
		for (const auto& view : Model->rows())
			renderRow(view, skipInputRow && std::string(view.row->Cvar) == skipInputRow);
	}
	if (BindModel) {
		for (const auto& view : BindModel->rows())
			renderBindRow(view);
		renderBindConflict();
	}
	setStatusSummary();
	setBindStatusSummary();
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

// Bind action label resolution: Localization token first ("#Valve_X" may
// exist in our lang file), then the engine-provided label table
// (resource/valve_english.txt - the vanilla source, SC ships no localized
// copy), then the raw label text. Everything is escaped by the caller.
std::string SettingsPage::bindActionLabel(const services::settings::BindRowView& view) const {
	if (!view.action)
		return {};
	return bindLabelText(view.action->Label);
}

std::string SettingsPage::bindLabelText(const std::string& label) const {
	if (!label.empty() && label[0] == '#') {
		std::string out;
		if (framework::Localization::translate(out, label.substr(1)))
			return out;
		if (BindModel && BindModel->lookupLabel(label.substr(1), out))
			return out;
		return label;
	}
	return label;
}

void SettingsPage::renderBindRow(const services::settings::BindRowView& view) {
	Rml::ElementDocument* doc = document();
	if (!doc || !view.action)
		return;
	Rml::Element* rowNode
		= doc->GetElementById((std::string("bindrow-") + std::to_string(view.actionIndex)).c_str());
	if (!rowNode)
		return;
	// The capturing row highlights its key list (class, not a display
	// property - the color lands immediately).
	const bool capturing = BindModel && BindModel->capturing()
		&& BindModel->captureRow() == view.actionIndex;
	rowNode->SetClass("bind-capturing", capturing);
	if (Rml::Element* label = rowNode->QuerySelector(".bind-label"))
		label->SetInnerRML(framework::TextUtil::escapeRml(bindActionLabel(view)));

	// Keys in draft order; "unbound" placeholder keeps the row readable.
	std::string keys;
	for (size_t i = 0; i < view.draftKeyNames.size(); ++i) {
		if (i)
			keys += ", ";
		keys += view.draftKeyNames[i];
	}
	if (view.draftKeyNames.empty())
		keys = translated("#Settings_BindNone");
	if (Rml::Element* keysNode = rowNode->QuerySelector(".bind-keys"))
		keysNode->SetInnerRML(framework::TextUtil::escapeRml(keys));

	// Buttons: capture text flips while this row is capturing; clear is the
	// unbind-all-keys affordance of the row (drafts only until Apply).
	if (Rml::Element* button = rowNode->QuerySelector(".bind-capture-btn"))
		button->SetInnerRML(framework::TextUtil::escapeRml(
			translated(capturing ? "#Settings_BindCancelCapture" : "#Settings_BindCapture")));
	if (Rml::Element* button = rowNode->QuerySelector(".bind-clear-btn"))
		button->SetInnerRML(framework::TextUtil::escapeRml(translated("#Settings_BindClear")));

	std::string status;
	if (capturing) {
		status = translated("#Settings_BindPressKey");
	} else {
		switch (view.state) {
		case services::settings::BindRowState::kDirty:
			status = translated("#Settings_BindDirty");
			break;
		case services::settings::BindRowState::kQueued:
			status = translated("#Settings_BindQueued");
			break;
		case services::settings::BindRowState::kApplyFailed:
			status = translated("#Settings_BindFailed");
			break;
		case services::settings::BindRowState::kUnavailable:
			status = translated("#Settings_BindUnavailable");
			break;
		case services::settings::BindRowState::kClean:
			break;
		}
	}
	if (Rml::Element* statusNode = rowNode->QuerySelector(".bind-status"))
		statusNode->SetInnerRML(framework::TextUtil::escapeRml(status));
}

// The conflict confirmation layer is part of the projection: it appears
// while the model holds a pending conflict (capture landed on a bound key)
// and disappears once resolveConflict clears it. Safe default focus sits on
// the non-destructive entry.
void SettingsPage::renderBindConflict() {
	Rml::ElementDocument* doc = document();
	if (!doc || !BindModel)
		return;
	Rml::Element* layer = doc->GetElementById(kLayerBindConflict);
	if (!layer)
		return;
	if (BindModel->conflictPending()) {
		// The keyname and the other action's label ride the layer text, each
		// piece escaped on its own (the label may carry user-facing engine
		// strings; nothing here is ever injected unescaped).
		if (Rml::Element* node = doc->GetElementById("bind-conflict-text"))
			node->SetInnerRML(framework::TextUtil::escapeRml(translated("#Settings_BindConflictLead"))
				+ " " + framework::TextUtil::escapeRml(BindModel->conflictKeyName()) + " "
				+ framework::TextUtil::escapeRml(translated("#Settings_BindBoundTo")) + " "
				+ framework::TextUtil::escapeRml(bindLabelText(BindModel->conflictOtherLabel())) + "?");
		if (ActiveLayer != kLayerBindConflict)
			showLayer(kLayerBindConflict);
	} else if (ActiveLayer == kLayerBindConflict) {
		hideLayers();
	}
}

void SettingsPage::setBindStatusSummary() {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	Rml::Element* status = doc->GetElementById("bind-status");
	if (!status)
		return;
	std::string text;
	if (!BindModel || !BindModel->readsAvailable()) {
		text = translated("#Settings_BindUnavailable");
	} else if (BindModel->conflictPending()) {
		text = translated("#Settings_BindConflictSummary");
	} else if (!BindModel->isDirty()) {
		text = translated("#Settings_BindClean");
	} else {
		text = translated("#Settings_BindDirtySummary");
	}
	status->SetInnerRML(framework::TextUtil::escapeRml(text));
}

void SettingsPage::applyAndReport() {
	if (!document())
		return;
	if (Model) {
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
	}
	if (BindModel) {
		// Bind writes ride the engine command buffer: kApplied rows already
		// match the truth, kQueued rows resolve on the next frames (the
		// module verifies per tick and re-renders on change).
		BindModel->apply();
	}
	render();
}

bool SettingsPage::isDirty() const {
	const bool settingsDirty = Model && Model->isDirty();
	const bool bindsDirty = BindModel && BindModel->isDirty();
	return settingsDirty || bindsDirty;
}

void SettingsPage::requestLeave() {
	if (!Model)
		return;
	if (!isDirty()) { // both models: a pending bind draft also blocks a silent exit
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

namespace {
// Safe default focus per layer: the non-destructive entry (cancel/stay/
// decline), never the action that writes or discards.
const char* safeFocusForLayer(const char* id) {
	if (std::string(id) == kLayerReset)
		return "reset-no";
	if (std::string(id) == kLayerBindConflict)
		return "bind-conflict-no";
	if (std::string(id) == kLayerBindReset)
		return "bind-reset-no";
	return "leave-stay";
}
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
	if (Rml::Element* focus = doc->GetElementById(safeFocusForLayer(id)))
		focus->Focus();
}

void SettingsPage::hideLayers() {
	Rml::ElementDocument* doc = document();
	if (!doc)
		return;
	for (const char* id : {kLayerReset, kLayerLeave, kLayerBindConflict, kLayerBindReset}) {
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
		if (Rml::Element* focus = doc->GetElementById(safeFocusForLayer(ActiveLayer.c_str())))
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
		// The discard IS the cancel: no write, drafts gone (both models),
		// then return.
		if (Model)
			Model->cancel();
		if (BindModel)
			BindModel->cancel();
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
	} else if (which == kActionBindCapture && !rowKey.empty()) {
		// Start a capture session: the model takes the row, the module takes
		// the router token (kCapture tier) and the InputBridge clamp. A
		// click on an already-capturing row is a no-op (the model refuses a
		// second session).
		if (BindModel && BindModel->beginCapture(std::atoi(rowKey.c_str()))) {
			const ActionSink action2 = Sink;
			if (action2)
				action2(kActionBindCaptureBegin);
		}
		render();
	} else if (which == kActionBindClear && !rowKey.empty()) {
		if (BindModel)
			BindModel->clearActionDraft(std::atoi(rowKey.c_str()));
		render();
	} else if (which == kActionBindRemove && !rowKey.empty()) {
		// data-row carries "<action>:<keyname>" for the single-key remove.
		const auto sep = rowKey.find(':');
		if (BindModel && sep != Rml::String::npos) {
			BindModel->removeKeyFromAction(std::atoi(rowKey.substr(0, sep).c_str()),
				rowKey.substr(sep + 1).c_str());
		}
		render();
	} else if (which == kActionBindReset) {
		// Bind defaults always confirm first; without a bind model the
		// whole section is absent, so the intent is a no-op (fail-safe).
		if (BindModel)
			showLayer(kLayerBindReset);
	} else if (which == kActionBindResetYes) {
		if (BindModel)
			BindModel->resetDefaults();
		hideLayers();
		render();
	} else if (which == kActionBindResetNo) {
		hideLayers();
	} else if (which == kActionBindConflictYes) {
		if (BindModel)
			BindModel->resolveConflict(true);
		render(); // the layer hides itself once the conflict is cleared
	} else if (which == kActionBindConflictNo) {
		if (BindModel)
			BindModel->resolveConflict(false);
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
