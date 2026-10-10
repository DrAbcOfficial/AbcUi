// panels::FlashlightPanel implementation - see FlashlightPanel.h.
// from: ABCEnchance/src/hud/vgui/flashlight.* (GBK->UTF-8).
#include <metahook.h>

#include "FlashlightPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../framework/ColorUtil.h"

namespace panels {

namespace {
// Old scheme FlashLight.IconFgColor falls back to Panel.FgColor = "DullWhite"
// (142 142 142). Grey input means the old hue *= ratio shift was a no-op in
// the shipped scheme - the colour path is ported anyway for custom schemes.
constexpr float kIconColor[3] = {142.0f, 142.0f, 142.0f};
} // namespace

FlashlightPanel::FlashlightPanel()
	: HudPanel("flashlight", "abcui/rml/flashlight.rml") {}

void FlashlightPanel::onDocumentReady() {
	subscribe(netmsg::Flashlight, [this](bool on, int battery) {
		setFlashLight(on, battery);
	});
	subscribe(netmsg::FlashBat, [this](int battery) {
		setFlashBattery(battery);
	});
	// Old split: the UpdateClientData handler re-showed on suit alone, the
	// HideWeapon/HideHUD token path applied the flashlight hide bit. Suit
	// state may predate the panel (see HealthPanel note).
	subscribe(hudstate::SuitChanged, [this](bool) {
		updateVisibility();
	});
	subscribe(hudstate::HideTokenChanged, [this](int) {
		applyHideToken();
	});
	updateVisibility();
}

void FlashlightPanel::reset() {
	// Old Reset: hide + battery back to full.
	hide();
	setFlashBattery(100);
}

void FlashlightPanel::updateVisibility() {
	// Old g_EventHudUpdateClientData: suit alone gates the panel here; hide
	// tokens only apply through their own path (they arrive as messages and
	// are re-evaluated there, while weapon-bit changes keep re-showing).
	if (hudstate::hasSuit())
		show();
	else
		hide();
}

void FlashlightPanel::applyHideToken() {
	// Old hudhideCallback: ShowPanel(!HIDEFLASHLIGHT) while suited, plus the
	// Viewport-wide hide-all (old SetVisible(false) on the HIDEALL bit).
	const bool visible = hudstate::hasSuit() && !hudstate::isHudHidden(hudstate::kHideFlashlight) &&
		!hudstate::isHudHidden(hudstate::kHideAll);
	if (visible)
		show();
	else
		hide();
}

void FlashlightPanel::setFlashLight(bool on, int battery) {
	if (Rml::Element* onImage = element("on-image"))
		onImage->SetProperty("visibility", on ? "visible" : "hidden");
	if (Rml::Element* offImage = element("off-image"))
		offImage->SetProperty("visibility", on ? "hidden" : "visible");
	setFlashBattery(battery);
}

void FlashlightPanel::setFlashBattery(int battery) {
	const float ratio = std::clamp(static_cast<float>(battery) / 100.0f, 0.0f, 1.0f);
	if (Rml::Element* wrap = element("power-wrap"))
		wrap->SetProperty("width", Rml::String(std::to_string(ratio * 23.0f)) + "dp");

	// Old SetFlashBattery: icon colour hue *= ratio (green -> red).
	float hsv[3];
	framework::ColorUtil::rgbToHsv(kIconColor, hsv);
	hsv[0] *= ratio;
	float rgb[3];
	framework::ColorUtil::hsvToRgb(hsv, rgb);
	char buffer[32];
	if (Rml::Element* bar = element("power-bar"))
		bar->SetProperty("image-color", framework::ColorUtil::format(rgb, buffer));

	char text[32];
	sprintf_s(text, "%d%%", battery);
	if (Rml::Element* message = element("message"))
		message->SetInnerRML(text);
}

}
