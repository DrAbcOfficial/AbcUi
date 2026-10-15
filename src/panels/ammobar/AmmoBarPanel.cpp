// panels::AmmoBarPanel implementation - see AmmoBarPanel.h.
// from: ABCEnchance/src/hud/vgui/ammobar.* (GBK->UTF-8).
#include <metahook.h>

#include "AmmoBarPanel.h"

#include <RmlUi/Core/Element.h>

#include <cstdio>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../data/WeaponResource.h"

namespace panels {

namespace {
// Old CAmmoPanel::WEAPONSTATE / ITEM_FLAG bits used by RefreshAmmo.
constexpr int kStateValid = 1 << 0;
constexpr int kStateUzi = 1 << 2;
constexpr int kUziAkimboFlag = 32;
} // namespace

AmmoBarPanel::AmmoBarPanel()
	: HudPanel("ammobar", "abc/abcui/rml/ammobar.rml") {}

void AmmoBarPanel::onDocumentReady() {
	subscribe(netmsg::CurWeapon, [this](int state, int id, int, int) {
		if (state > 0) {
			// Old g_EventCurWeapon handler: weapon lookup may miss (server
			// sent no WeaponList yet) - RefreshAmmo then hides the bar.
			HandledWeapon = weaponres::getWeapon(id);
			refreshAmmo();
		}
	});
	subscribe(netmsg::AmmoX, [this](int, int) {
		refreshAmmo();
	});
	subscribe(hudstate::SuitChanged, [this](bool) {
		updateVisibility();
		refreshAmmo();
	});
	subscribe(hudstate::HideTokenChanged, [this](int) {
		applyHideToken();
	});
	subscribe(hudstate::SpectateChanged, [this](bool) {
		updateVisibility();
	});
}

void AmmoBarPanel::reset() {
	// Old Reset: hide + drop the weapon reference.
	hide();
	HandledWeapon = nullptr;
}

void AmmoBarPanel::updateVisibility() {
	applyHideToken();
}

void AmmoBarPanel::applyHideToken() {
	// Old hudhideCallback: ShowPanel(!HIDEWEAPONS) while suited; the
	// Viewport-wide HIDEALL bit hides the ammo bar with everything else.
	// RefreshAmmo re-hides when the weapon carries no ammo types.
	const bool visible = hudstate::hasSuit() && !hudstate::isHudHidden(hudstate::kHideWeapons) &&
		!hudstate::isHudHidden(hudstate::kHideAll) && !hudstate::isSpectate() && HandledWeapon;
	if (visible)
		show();
	else
		hide();
}

void AmmoBarPanel::refreshAmmo() {
	if (!HandledWeapon) {
		updateVisibility();
		return;
	}
	applyHideToken();
	if (!(HandledWeapon->state & kStateValid))
		return;

	char buf[64];
	if (HandledWeapon->ammoType > 0) {
		if (Rml::Element* icon = element("ammo1-icon")) {
			if (HandledWeapon->ammo.valid) {
				const wrect_t& rc = HandledWeapon->ammo.rect;
				icon->SetProperty("visibility", "visible");
				icon->SetAttribute("src", "spricon://" + HandledWeapon->ammo.atlas + "/" +
					std::to_string(rc.left) + "," + std::to_string(rc.top) + "," +
					std::to_string(rc.right) + "," + std::to_string(rc.bottom));
			}
			else {
				icon->SetProperty("visibility", "hidden");
			}
		}
		if (Rml::Element* label = element("ammo1"))
			label->SetProperty("visibility", "visible");
		const int ammoLeft = weaponres::countAmmo(HandledWeapon->ammoType);
		if (HandledWeapon->clip >= 0) {
			if ((HandledWeapon->state & kStateUzi) && (HandledWeapon->flags & kUziAkimboFlag) &&
				HandledWeapon->clip2 >= 0)
				sprintf_s(buf, "%dx%d/%d", HandledWeapon->clip2, HandledWeapon->clip, ammoLeft);
			else
				sprintf_s(buf, "%d/%d", HandledWeapon->clip, ammoLeft);
		}
		else {
			sprintf_s(buf, "%d", ammoLeft);
		}
		if (Rml::Element* label = element("ammo1"))
			label->SetInnerRML(buf);
	}
	else {
		if (Rml::Element* icon = element("ammo1-icon"))
			icon->SetProperty("visibility", "hidden");
		if (Rml::Element* label = element("ammo1"))
			label->SetProperty("visibility", "hidden");
	}
	if (HandledWeapon->ammo2Type > 0 && (HandledWeapon->state & kStateValid)) {
		if (Rml::Element* icon = element("ammo2-icon")) {
			if (HandledWeapon->ammo2.valid) {
				const wrect_t& rc = HandledWeapon->ammo2.rect;
				icon->SetProperty("visibility", "visible");
				icon->SetAttribute("src", "spricon://" + HandledWeapon->ammo2.atlas + "/" +
					std::to_string(rc.left) + "," + std::to_string(rc.top) + "," +
					std::to_string(rc.right) + "," + std::to_string(rc.bottom));
			}
			else {
				icon->SetProperty("visibility", "hidden");
			}
		}
		if (Rml::Element* label = element("ammo2"))
			label->SetProperty("visibility", "visible");
		if (Rml::Element* slash = element("slash"))
			slash->SetProperty("visibility", "visible");
		const int ammoLeft2 = weaponres::countAmmo(HandledWeapon->ammo2Type);
		if (HandledWeapon->clip2 >= 0)
			sprintf_s(buf, "%d/%d", HandledWeapon->clip2, ammoLeft2);
		else
			sprintf_s(buf, "%d", ammoLeft2);
		if (Rml::Element* label = element("ammo2"))
			label->SetInnerRML(buf);
	}
	else {
		if (Rml::Element* icon = element("ammo2-icon"))
			icon->SetProperty("visibility", "hidden");
		if (Rml::Element* label = element("ammo2"))
			label->SetProperty("visibility", "hidden");
		if (Rml::Element* slash = element("slash"))
			slash->SetProperty("visibility", "hidden");
	}
	if (HandledWeapon->ammoType <= 0 && HandledWeapon->ammo2Type <= 0)
		hide();
}

}
