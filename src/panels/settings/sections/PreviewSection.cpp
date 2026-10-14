#include "panels/settings/SettingsPage.h"
#include "services/settings/SettingsModel.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Core.h>
#include <algorithm>

namespace panels::settings {
void SettingsPage::renderPreviews() {
	if (!document() || !Model)
		return;
	auto Text = [&](const char* Cvar) -> std::string {
		const auto* View = Model->find(Cvar);
		return View && !View->missing ? View->draftText : "";
	};
	auto Color = [&](const char* Cvar) {
		const auto* View = Model->find(Cvar);
		return View ? std::to_string(static_cast<int>(std::clamp(View->draftValue, 0.0f, 255.0f))) : "0";
	};
	auto Set = [&](const char* Id, const std::string& Uri) {
		if (auto* Image = document()->GetElementById(Id)) {
			const auto Previous = Image->GetAttribute<Rml::String>("src", "");
			if (Previous != Uri) {
				Image->SetAttribute("src", Uri);
				// Rml caches file textures globally. Retire the old palette
				// variant so dragging colors cannot accumulate GPU textures.
				if (!Previous.empty()) Rml::ReleaseTexture(Previous);
			}
		}
	};
	Set("model-preview", "preview://model/" + Text("model") + "/" + Color("topcolor") + "/" + Color("bottomcolor"));
	Set("spray-preview", "preview://spray/" + Text("cl_logofile") + "/" + Text("cl_logocolor"));
}
void SettingsPage::releasePreviews() {
	if (!document()) return;
	for (const auto* Id : {"model-preview", "spray-preview"})
		if (auto* Image = document()->GetElementById(Id)) {
			const auto Source = Image->GetAttribute<Rml::String>("src", "");
			if (!Source.empty()) Rml::ReleaseTexture(Source);
		}
}
} // namespace panels::settings
