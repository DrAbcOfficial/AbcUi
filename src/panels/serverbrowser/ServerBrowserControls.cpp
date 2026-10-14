#include "ServerBrowserPage.h"
#include "framework/Localization.h"
#include "framework/TextUtil.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <charconv>

namespace panels::serverbrowser {
namespace {
std::string label(const char* Token) {
	std::string Text;
	return framework::Localization::translate(Text, Token) ? Text : std::string("#") + Token;
}
Rml::Element* actionNode(Rml::Element* Node) {
	for (; Node; Node = Node->GetParentNode())
		if (Node->HasAttribute("data-action")) return Node;
	return nullptr;
}
}
bool ServerBrowserPage::closeSubpage() {
	if (!FilterOpen) return false;
	FilterOpen = false;
	renderControls();
	refocusDefault();
	return true;
}
void ServerBrowserPage::renderControls() {
	if (!document() || !List || InControls) return;
	InControls = true;
	auto Node = [&](const char* Id) { return document()->GetElementById(Id); };
	if (auto* Panel = Node("sb-filter-panel")) Panel->SetProperty("display", FilterOpen ? "block" : "none");
	if (auto* Panel = Node("sb-panel")) Panel->SetClass("sb-blacklist-mode", List->blockedOnly());
	if (auto* Panel = Node("sb-blacklist-panel")) Panel->SetProperty("display", List->blockedOnly() ? "block" : "none");
	if (auto* Tab = Node("sb-tab-blacklist")) Tab->SetClass("sb-active", List->blockedOnly());
	for (const auto* Id : {"sb-filter-empty", "sb-filter-full", "sb-filter-secure", "sb-filter-password"}) {
		bool On = std::string(Id) == "sb-filter-empty" ? DraftFilter.hideEmpty
			: std::string(Id) == "sb-filter-full" ? DraftFilter.hideFull
			: std::string(Id) == "sb-filter-secure" ? DraftFilter.secureOnly : DraftFilter.NoPassword;
		if (auto* Button = Node(Id)) Button->SetClass("sb-on", On);
	}
	if (Blacklist && Node("sb-blacklist-rules")) {
		std::string Markup;
		for (const auto& Rule : Blacklist->rules()) {
			Markup += "<div class=\"sb-blacklist-rule\"><span>"
				+ framework::TextUtil::escapeRml(Rule.address() + " " + Rule.Name)
				+ "</span><button data-action=\"sb-rule-remove\" data-address=\""
				+ Rule.address() + "\">" + framework::TextUtil::escapeRml(label("SB_Unblock")) + "</button></div>";
		}
		Node("sb-blacklist-rules")->SetInnerRML(Markup);
	}
	if (auto* Status = Node("sb-blacklist-status"))
		Status->SetInnerRML(framework::TextUtil::escapeRml(RuleStatus));
	if (!RuleStatus.empty() && Node("sb-status"))
		Node("sb-status")->SetInnerRML(framework::TextUtil::escapeRml(RuleStatus));
	InControls = false;
}

bool ServerBrowserPage::handleControls(Rml::Event& Event) {
	auto* Entry = actionNode(Event.GetTargetElement());
	if (!Entry || !List || !Query) return false;
	const auto Action = Entry->GetAttribute<Rml::String>("data-action", "");
	if (Action == "sb-filters") {
		DraftFilter = List->filter();
		FilterOpen = true;
		InControls = true;
		auto SetText = [&](const char* Id, const std::string& Text) {
			if (auto* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(document()->GetElementById(Id)))
				Control->SetValue(Text);
		};
		SetText("sb-filter-name", DraftFilter.Name);
		SetText("sb-filter-map", DraftFilter.Map);
		if (auto* Ping = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(document()->GetElementById("sb-filter-ping"))) {
			Ping->RemoveAll();
			Ping->Add(DraftFilter.MaxPing ? std::to_string(DraftFilter.MaxPing) + " ms" : label("SB_Any"),
				std::to_string(DraftFilter.MaxPing));
			for (int Max : {0, 50, 100, 150, 250, 350, 500}) {
				if (Max == DraftFilter.MaxPing) continue;
				Ping->Add(Max ? std::to_string(Max) + " ms" : label("SB_Any"), std::to_string(Max));
			}
			Ping->SetSelection(0);
		}
		InControls = false;
		renderControls();
		if (auto* Name = document()->GetElementById("sb-filter-name")) Name->Focus();
		return true;
	}
	if (Action == "sb-filters-cancel") { closeSubpage(); return true; }
	if (Action == "sb-filters-apply") {
		if (!FilterOpen) return true;
		List->setFilter(DraftFilter);
		closeSubpage();
		const auto Callback = Sink;
		if (Callback) Callback("sb-filters-apply");
		return true;
	}
	if (Action == "sb-filter-empty" || Action == "sb-filter-full" || Action == "sb-filter-secure"
		|| Action == "sb-filter-password") {
		if (!FilterOpen) return true;
		if (Action == "sb-filter-empty") DraftFilter.hideEmpty = !DraftFilter.hideEmpty;
		if (Action == "sb-filter-full") DraftFilter.hideFull = !DraftFilter.hideFull;
		if (Action == "sb-filter-secure") DraftFilter.secureOnly = !DraftFilter.secureOnly;
		if (Action == "sb-filter-password") DraftFilter.NoPassword = !DraftFilter.NoPassword;
		renderControls();
		return true;
	}
	if (FilterOpen) return true; // background rows/actions cannot bypass the subpage
	if (Action == "sb-tab-blacklist") {
		List->setBlockedOnly(true);
		Query->cancelDetail();
		tick();
		return true;
	}
	if (Action.rfind("sb-tab-", 0) == 0) {
		RuleStatus.clear();
		List->setBlockedOnly(false);
		return false; // fixed query intent dispatched by the regular handler
	}
	if (Action == "sb-block" || Action == "sb-block-ip" || Action == "sb-rule-add" || Action == "sb-rule-remove") {
		if (!Blacklist) return true;
		bool Ok = false;
		if (Action == "sb-rule-remove")
			Ok = Blacklist->remove(Entry->GetAttribute<Rml::String>("data-address", ""));
		else if (Action == "sb-rule-add") {
			if (auto* Input = rmlui_dynamic_cast<Rml::ElementFormControl*>(document()->GetElementById("sb-blacklist-address")))
				Ok = Blacklist->add(Input->GetValue());
		} else if (const auto* Server = List->selectedEntry(*Query)) {
			services::serverbrowser::BlacklistRule Rule{Server->ip,
				static_cast<uint16_t>(Action == "sb-block-ip" ? 0 : Server->connectionPort ? Server->connectionPort : Server->queryPort)};
			Ok = Blacklist->add(Rule.address(), Server->name);
		}
		RuleStatus = label(Ok ? "SB_BlacklistSaved" : "SB_BlacklistError");
		Projected = false;
		tick();
		return true;
	}
	return false;
}
void ServerBrowserPage::handleChange(Rml::Event& Event) {
	if (!FilterOpen || InControls || !Event.GetTargetElement()) return;
	const auto Id = Event.GetTargetElement()->GetId();
	const auto Text = Event.GetParameter<Rml::String>("value", "");
	if (Text.size() > 256) return;
	if (Id == "sb-filter-name") DraftFilter.Name = Text;
	if (Id == "sb-filter-map") DraftFilter.Map = Text;
	if (Id == "sb-filter-ping") {
		int Value = 0;
		auto R = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
		if (R.ec == std::errc() && R.ptr == Text.data() + Text.size() && Value >= 0 && Value <= 500)
			DraftFilter.MaxPing = Value;
	}
}
} // namespace panels::serverbrowser
