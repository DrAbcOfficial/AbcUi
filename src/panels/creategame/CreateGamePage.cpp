// panels::creategame implementation - see CreateGamePage.h.
#include "CreateGamePage.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>

#include "../../framework/Localization.h"
#include "../../framework/TextUtil.h"

#include <cstdio>
#include <cstdlib>

namespace panels::creategame {

namespace {

constexpr const char* kActionApply = "cg-apply";
constexpr const char* kActionCancel = "cg-cancel";

Rml::Element* actionNode(Rml::Element* node) {
	for (Rml::Element* it = node; it; it = it->GetParentNode()) {
		if (it->HasAttribute("data-action"))
			return it;
	}
	return nullptr;
}

// Local table first (project "#CG_*" tokens), then the raw text - an unknown
// token is shown instead of a blank line (honest fallback).
std::string resolve(const std::string& text) {
	if (text.empty())
		return {};
	if (text[0] != '#')
		return text;
	std::string out;
	if (framework::Localization::translate(out, text.substr(1)))
		return out;
	return text;
}

} // namespace

CreateGamePage::CreateGamePage()
	: InteractiveDocument("menucreategame", "abcui/rml/creategame.rml", true), Listener(this) {}

CreateGamePage::~CreateGamePage() {
	close();
}

void CreateGamePage::onDocumentReady() {
	if (Rml::ElementDocument* doc = document()) {
		addListener(doc, "click", &Listener);
		addListener(doc, "change", &Listener);
	}
}

Rml::Element* CreateGamePage::node(const char* id) const {
	Rml::ElementDocument* doc = document();
	return doc ? doc->GetElementById(id) : nullptr;
}

void CreateGamePage::prepare(const services::creategame::MapCatalog& catalog,
	const char* defaultName) {
	Applied = false;
	Lan = false;
	buildOptions();
	Maps.clear();
	for (std::size_t i = 0; i < catalog.size(); ++i)
		Maps.push_back(catalog.nameAt(i));

	// The map options are rebuilt per opening (the shipped dialog fills its
	// ComboBox on construction). Add() with no matching selection auto-selects
	// the first item at the next Update (T13 pin) - the explicit SetSelection
	// below is the projection, never a change dispatch.
	if (Rml::ElementFormControlSelect* select =
			rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(node("cg-map"))) {
		select->RemoveAll();
		for (const std::string& map : Maps)
			select->Add(framework::TextUtil::escapeRml(map), map);
		if (!Maps.empty())
			select->SetSelection(0);
	}

	if (Rml::ElementFormControl* control =
			rmlui_dynamic_cast<Rml::ElementFormControl*>(node("cg-players")))
		control->SetValue("8");  // settings.scr default (NUMBER 1..32)
	if (Rml::ElementFormControl* control =
			rmlui_dynamic_cast<Rml::ElementFormControl*>(node("cg-name"))) {
		std::string name = defaultName ? defaultName : "";
		if (name.empty())
			name = resolve("#CG_DefaultName");
		control->SetValue(name);
	}
	if (Rml::ElementFormControl* control =
			rmlui_dynamic_cast<Rml::ElementFormControl*>(node("cg-password")))
		control->SetValue("");

	showHint(nullptr);
	if (auto* Control = node("cg-players-value"))
		Control->SetInnerRML("8");
}

void CreateGamePage::showHint(const char* text) {
	if (text && *text)
		Applied = false; // rejected form can be corrected, successful launches stay latched
	Rml::Element* hint = node("cg-hint");
	if (!hint)
		return;
	const std::string resolved = text ? resolve(text) : std::string();
	hint->SetInnerRML(framework::TextUtil::escapeRml(resolved));
	hint->SetProperty("display", resolved.empty() ? "none" : "block");
}

std::string CreateGamePage::selectedMap() const {
	if (Rml::ElementFormControlSelect* select =
			rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(const_cast<Rml::Element*>(node("cg-map"))))
		return select->GetValue();
	return {};
}

std::string CreateGamePage::playersText() const {
	if (Rml::ElementFormControlInput* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
			const_cast<Rml::Element*>(node("cg-players"))))
		return std::to_string(static_cast<int>(std::strtof(input->GetValue().c_str(), nullptr)));
	return {};
}

std::string CreateGamePage::serverName() const {
	if (Rml::ElementFormControlInput* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
			const_cast<Rml::Element*>(node("cg-name"))))
		return input->GetValue();
	return {};
}

std::string CreateGamePage::password() const {
	if (Rml::ElementFormControlInput* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
			const_cast<Rml::Element*>(node("cg-password"))))
		return input->GetValue();
	return {};
}

void CreateGamePage::refocusDefault() {
	if (Rml::Element* select = node("cg-map"))
		select->Focus();
}

void CreateGamePage::handleClick(Rml::Event& event) {
	Rml::Element* entry = actionNode(event.GetTargetElement());
	if (!entry)
		return;
	const Rml::String action = entry->GetAttribute<Rml::String>("data-action", "");
	if (action.empty())
		return;
	if (action == "cg-lan") {
		Lan = !Lan;
		entry->SetInnerRML(resolve(Lan ? "#Settings_On" : "#Settings_Off"));
		return;
	}
	if (action == kActionApply) {
		if (Applied)
			return;  // one report per opening (the backend latch is behind it)
		Applied = true;
	} else if (action != kActionCancel) {
		return;
	}
	const ActionSink Action = Sink;
	if (Action)
		Action(action.c_str());
}

void CreateGamePage::PageListener::ProcessEvent(Rml::Event& event) {
	if (event.GetType() == "change")
		Owner->handleChange(event);
	else
		Owner->handleClick(event);
}

}
