#include "SettingsPage.h"
#include "SettingsUi.h"
#include "framework/TextUtil.h"
#include "services/settings/KeyBindModel.h"
#include "services/settings/SettingsModel.h"
#include "services/settings/VoiceModel.h"

#include <RmlUi/Core/ElementDocument.h>
#include <algorithm>

namespace panels::settings {

const services::uiconfig::SettingsPageDefinition* SettingsPage::currentPage() const {
	if (Pages) {
		for (const auto& Page : *Pages)
			if (Page.Id == ActivePage)
				return &Page;
	}
	return nullptr;
}

bool SettingsPage::hasComponent(const char* Name) const {
	// Unconfigured test fixtures retain the legacy all-in-one projection;
	// production always supplies Pages, including an empty failed catalog.
	const auto* Page = currentPage();
	return !Pages || (Page && Page->hasComponent(Name));
}

void SettingsPage::buildNavigation() {
	if (!document() || !Pages)
		return;
	auto* Navigation = document()->GetElementById("settings-navigation");
	if (!Navigation)
		return;
	std::string Markup;
	for (const auto& Page : *Pages) {
		Markup += "<button id=\"settings-tab-" + Page.Id
			+ "\" data-action=\"settings-page\" data-page=\"" + Page.Id + "\">"
			+ framework::TextUtil::escapeRml(detail::translated(Page.Label.c_str())) + "</button>";
	}
	Navigation->SetInnerRML(Markup);
	if (!currentPage())
		ActivePage = Pages->empty() ? "" : Pages->front().Id;
	projectPage();
}

bool SettingsPage::selectPage(const std::string& Id) {
	if (!Pages || layerVisible() || (BindModel && BindModel->capturing()))
		return false;
	auto It = std::find_if(Pages->begin(), Pages->end(), [&](const auto& Page) { return Page.Id == Id; });
	if (It == Pages->end())
		return false;
	if (Id != ActivePage && VoiceModel_) {
		VoiceModel_->stopTest();
		renderVoiceSection();
	}
	ActivePage = Id;
	projectPage();
	if (document()) {
		if (auto* Panel = document()->GetElementById("panel"))
			Panel->SetScrollTop(0);
		if (auto* Tab = document()->GetElementById("settings-tab-" + Id))
			Tab->Focus();
	}
	return true;
}

void SettingsPage::projectPage() {
	if (!document() || !Pages)
		return;
	for (const auto& Page : *Pages) {
		if (auto* Rows = document()->GetElementById("settings-rows-" + Page.Id))
			Rows->SetProperty("display", Page.Id == ActivePage ? "block" : "none");
		if (auto* Tab = document()->GetElementById("settings-tab-" + Page.Id))
			Tab->SetClass("selected", Page.Id == ActivePage);
	}
	auto Show = [&](const char* Id, bool Visible) {
		if (auto* Node = document()->GetElementById(Id))
			Node->SetProperty("display", Visible ? "block" : "none");
	};
	for (const char* Id : {"bind-title", "bind-status", "bindrows", "bind-reset"})
		Show(Id, hasComponent("bindings"));
	for (const char* Id : {"video-title", "video-section"})
		Show(Id, hasComponent("video"));
	for (const char* Id : {"voice-title", "voice-section"})
		Show(Id, hasComponent("voice"));
	if (auto* Title = document()->GetElementById("title")) {
		const auto* Page = currentPage();
		Title->SetInnerRML(framework::TextUtil::escapeRml(
			detail::translated(Page ? Page->Label.c_str() : "#Settings_Title")));
	}
	Show("settings-reset", currentPage() && !currentPage()->Cvars.empty());
}

void SettingsPage::resetCurrentPage() {
	if (!Model)
		return;
	if (!Pages)
		Model->resetDefaults();
	else if (const auto* Page = currentPage())
		Model->resetDefaults(Page->Cvars);
	// Hardware/display controls have no verified factory-default provider.
	// Their own explicit transactions remain separate; never invent values.
}
} // namespace panels::settings
