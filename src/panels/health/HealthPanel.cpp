// panels::HealthPanel implementation - see HealthPanel.h.
// from: ABCEnchance/src/hud/vgui/health.* (GBK->UTF-8).
#include <metahook.h>

#include "HealthPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/ColorUtil.h"

namespace panels {

namespace health {

// Old CHealthPanel ctor defaults (persisted contract - never rename).
cvar_t* DangerHealth = nullptr;
cvar_t* DangerArmor = nullptr;

void registerCvars() {
	DangerHealth = abc::CREATE_CVAR("hud_danger_health", "45", abc::FCVAR_VALUE, nullptr);
	DangerArmor = abc::CREATE_CVAR("hud_danger_armor", "45", abc::FCVAR_VALUE, nullptr);
}

} // namespace health

namespace {

// Old scheme "Default" (0 255 0) and "Red" (255 0 0).
constexpr float kGreen[3] = {0.0f, 255.0f, 0.0f};
constexpr float kRed[3] = {255.0f, 0.0f, 0.0f};
// Bar geometry (old HealthPanel.res): 56dp full width.
constexpr float kBarWidth = 56.0f;

} // namespace

HealthPanel::HealthPanel()
	: HudPanel("health", "abcui/rml/health.rml") {}

void HealthPanel::onDocumentReady() {
	subscribe(netmsg::Health, [this](int value) {
		setHealth(value);
	});
	subscribe(netmsg::Battery, [this](int value) {
		setArmor(value);
	});
	subscribe(hudstate::SuitChanged, [this](bool) {
		updateVisibility();
	});
	subscribe(hudstate::HideTokenChanged, [this](int) {
		updateVisibility();
	});
	subscribe(hudstate::SpectateChanged, [this](bool) {
		updateVisibility();
	});
	subscribe(hudstate::LongJumpChanged, [this](bool longJump) {
		setLongJump(longJump);
	});
}

void HealthPanel::reset() {
	// Old Reset: hide + clear the longjump marker.
	hide();
	setLongJump(false);
}

void HealthPanel::updateVisibility() {
	const bool hideHealth = hudstate::isHudHidden(hudstate::kHideHealth);
	const bool hideBattery = hudstate::isHudHidden(hudstate::kHideBattery);
	const bool visible = hudstate::hasSuit() && (!hideBattery || !hideHealth) && !hudstate::isSpectate();
	if (visible)
		show();
	else
		hide();
	// Old SetHealthVisible/SetArmorVisible element groups.
	const char* healthGroup[] = {"health-icon", "health-label", "health-bar-bg", "health-bar-wrap"};
	const char* armorGroup[] = {"armor-icon", "armor-label", "armor-bar-bg", "armor-bar-wrap"};
	for (const char* id : healthGroup) {
		if (Rml::Element* el = element(id))
			el->SetProperty("visibility", hideHealth ? "hidden" : "visible");
	}
	for (const char* id : armorGroup) {
		if (Rml::Element* el = element(id))
			el->SetProperty("visibility", hideBattery ? "hidden" : "visible");
	}
}

void HealthPanel::setHealth(int health) {
	// Old g_EventHealth handler: while connected and not spectating, a
	// non-positive health clamps to 1 (spectator feeds report 0).
	if (health <= 0) {
		const cl_entity_t* local = gEngfuncs.GetLocalPlayer();
		if (local && !hudstate::isSpectate())
			health = 1;
	}
	if (Rml::Element* label = element("health-label"))
		label->SetInnerRML(std::to_string(health));
	applyDanger("health-label", "health-bar", "health-icon", health,
		health::DangerHealth ? health::DangerHealth->value : 45.0f);
	if (Rml::Element* wrap = element("health-bar-wrap")) {
		const float pct = std::clamp(health / 100.0f, 0.0f, 1.0f);
		wrap->SetProperty("width", Rml::String(std::to_string(pct * kBarWidth)) + "dp");
	}
}

void HealthPanel::setArmor(int armor) {
	if (Rml::Element* label = element("armor-label"))
		label->SetInnerRML(std::to_string(armor));
	applyDanger("armor-label", "armor-bar", "armor-icon", armor,
		health::DangerArmor ? health::DangerArmor->value : 45.0f);
	if (Rml::Element* wrap = element("armor-bar-wrap")) {
		const float pct = std::clamp(armor / 100.0f, 0.0f, 1.0f);
		wrap->SetProperty("width", Rml::String(std::to_string(pct * kBarWidth)) + "dp");
	}
}

void HealthPanel::applyDanger(const char* labelId, const char* barId, const char* iconId,
	int value, float dangerValue) {
	float rgb[3];
	if (dangerValue > 0.0f && value < dangerValue) {
		// Old GetDifferColor: hue pulled toward the danger red as the value
		// falls below the threshold (ratio = value / danger).
		framework::ColorUtil::differColor(kGreen, kRed, value / dangerValue, rgb);
	}
	else {
		rgb[0] = kGreen[0];
		rgb[1] = kGreen[1];
		rgb[2] = kGreen[2];
	}
	char buffer[32];
	const Rml::String color = framework::ColorUtil::format(rgb, buffer);
	if (Rml::Element* label = element(labelId))
		label->SetProperty("color", color);
	if (Rml::Element* bar = element(barId))
		bar->SetProperty("image-color", color);
	if (Rml::Element* icon = element(iconId))
		icon->SetProperty("image-color", color);
}

void HealthPanel::setLongJump(bool longJump) {
	if (Rml::Element* icon = element("longjump"))
		icon->SetProperty("visibility", longJump ? "visible" : "hidden");
}

}
