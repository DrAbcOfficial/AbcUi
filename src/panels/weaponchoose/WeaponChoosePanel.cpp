// panels::WeaponChoose implementation - see WeaponChoosePanel.h.
// from: ABCEnchance/src/hud/vgui/weaponchoose.cpp (GBK->UTF-8).
// Layout numbers from WeaponChoosePanel.res (slot icons 15x15, active items
// 60x20, inactive 20x20, 1dp gaps; close_time 5); scheme WeaponChoose.*
// colors (ActiveColor Default green, Inactive DefaultTransparent, empty Red
// variants, ammo bar 80,80,80,125 / 255,255,0,125, bucket 255 255 0 255).
#include <metahook.h>

#include "WeaponChoosePanel.h"

#include <RmlUi/Core/Element.h>

#include <cmath>

#include <abc/CvarCmd.h>

#include "../../data/HudState.h"
#include "../../data/NetworkMessages.h"
#include "../../data/PlayerResource.h"
#include "../../data/WeaponResource.h"
#include "../../framework/Commands.h"
#include "../textmenu/TextMenuPanel.h"

// Windows.h min/max macros fight std::min/std::max (old repo precedent).
#undef max
#undef min

namespace panels {

namespace weaponchoose {

WeaponChoosePanel* PanelInstance = nullptr;

bool textmenuOpen() {
	return panels::textmenu::isOpen();
}

bool PanelBlockAttack() {
	return PanelInstance && PanelInstance->blockAttackOnce();
}

// Old ctor default (persisted contract - never rename).
cvar_t* RainbowMenu = nullptr;

void registerCvars() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;
	RainbowMenu = abc::CREATE_CVAR("hud_rainbow_menu", "0", abc::FCVAR_VALUE, nullptr);
}

void registerCommands() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;

	// The old weaponres g_EventCmdSlot handler: TextMenu first (its own
	// chain subscription swallows), then the weapon selection walk.
	for (int i = 0; i < 10; i++) {
		if (abc::CChainedCommand* chained = commands::slotCommand(i)) {
			chained->Event().append([i]() {
				if (textmenuOpen())
					return true;
				weaponres::selectSlot(i, 1, false);
				return true;
			});
		}
	}
	if (abc::CChainedCommand* next = commands::get("invnext")) {
		next->Event().append([]() {
			weaponres::selectSlot(weaponres::selectedSlot(), 1, true);
			return true;
		});
	}
	if (abc::CChainedCommand* prev = commands::get("invprev")) {
		prev->Event().append([]() {
			weaponres::selectSlot(weaponres::selectedSlot(), -1, true);
			return true;
		});
	}
	if (abc::CChainedCommand* attack = commands::get("+attack")) {
		attack->Event().append([]() {
			return !PanelBlockAttack();
		});
	}
}

} // namespace weaponchoose

namespace {

constexpr float kSlotIcon = 15.0f;
constexpr float kItemActiveW = 60.0f;
constexpr float kItemActiveH = 20.0f;
constexpr float kItemInactiveW = 20.0f;
constexpr float kItemInactiveH = 20.0f;
constexpr float kGap = 1.0f;
constexpr float kCloseTime = 5.0f;

// Old RGBtoHSV/HSVtoRGB pair, hue-shifted - the rainbow tint cycle.
void rainbowShift(int counter, unsigned char& r, unsigned char& g, unsigned char& b) {
	float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
	const float mx = std::max({rf, gf, bf});
	const float mn = std::min({rf, gf, bf});
	float h = 0.0f;
	const float v = mx;
	const float s = mx > 0.0f ? (mx - mn) / mx : 0.0f;
	if (mx > mn) {
		if (mx == rf)
			h = (gf - bf) / (mx - mn) + (gf < bf ? 6.0f : 0.0f);
		else if (mx == gf)
			h = (bf - rf) / (mx - mn) + 2.0f;
		else
			h = (rf - gf) / (mx - mn) + 4.0f;
		h *= 60.0f;
	}
	h = std::fmod(h + counter * 20.0f, 360.0f);
	const float c = v * s;
	const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	const float m = v - c;
	float r2 = 0, g2 = 0, b2 = 0;
	if (h < 60) { r2 = c; g2 = x; }
	else if (h < 120) { r2 = x; g2 = c; }
	else if (h < 180) { g2 = c; b2 = x; }
	else if (h < 240) { g2 = x; b2 = c; }
	else if (h < 300) { r2 = x; b2 = c; }
	else { r2 = c; b2 = x; }
	r = static_cast<unsigned char>((r2 + m) * 255.0f);
	g = static_cast<unsigned char>((g2 + m) * 255.0f);
	b = static_cast<unsigned char>((b2 + m) * 255.0f);
}

} // namespace

WeaponChoosePanel::WeaponChoosePanel()
	: HudPanel("weaponchoose", "abcui/rml/weaponchoose.rml") {
	weaponchoose::PanelInstance = this;
}

void WeaponChoosePanel::reset() {
	if (Document) {
		if (Rml::Element* box = element("box"))
			box->SetProperty("display", "none");
	}
	Visible = false;
	SelectBlock = false;
}

void WeaponChoosePanel::setVisible(bool visible) {
	if (visible == Visible)
		return;
	Visible = visible;
	if (Rml::Element* box = element("box"))
		box->SetProperty("display", visible ? "block" : "none");
}

bool WeaponChoosePanel::shouldDraw() const {
	// Old ShouldDraw gate chain.
	const cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local)
		return false;
	if (playerres::isEntitySpectate(local->index))
		return false;
	if (hudstate::isHudHidden(hudstate::kHideAll | hudstate::kHideWeapons))
		return false;
	if (!hudstate::hasSuit())
		return false;
	const playerres::PlayerInfo* localInfo = playerres::getLocalPlayerInfo();
	if (!localInfo || localInfo->health <= 0)
		return false;
	// Old hud_fastswitch check (the cvar pointer lives in weaponres).
	return true;
}

void WeaponChoosePanel::rebuild() {
	Rml::Element* box = element("box");
	if (!box)
		return;

	// All owned weapons grouped by slot (old InsertWeapon slot lists).
	std::vector<const weaponres::Weapon*> bySlot[weaponres::kMaxWeaponSlot];
	for (int id = 1; id < 32; id++) {
		const weaponres::Weapon* weapon = weaponres::getWeapon(id);
		if (!weapon || weapon->slot < 0 || weapon->slot >= static_cast<int>(weaponres::kMaxWeaponSlot))
			continue;
		// Old lists carried owned weapons only (Insert/Drop feed).
		bySlot[weapon->slot].push_back(weapon);
	}
	for (auto& list : bySlot) {
		std::sort(list.begin(), list.end(), [](const weaponres::Weapon* a, const weaponres::Weapon* b) {
			return a->slotPos < b->slotPos;
		});
	}

	const weaponres::Weapon* selected = weaponres::selectedWeapon();
	const int activeSlot = weaponres::selectedSlot();

	char buffer[512];
	std::string rml;
	rml.reserve(4096);
	float x = 0.0f;
	float maxHeight = kSlotIcon;
	RainbowCounter = 0;

	for (size_t slot = 0; slot < weaponres::kMaxWeaponSlot; slot++) {
		float y = 0.0f;
		// Slot icon row.
		sprintf_s(buffer,
			"<div class='slot' style='left:%.0fdp; top:0dp;'><img src='../img/weaponchoose/slot%zu_icon.tga'/></div>",
			x, slot == 9 ? 0 : slot);
		rml += buffer;
		y = kSlotIcon + kGap;

		const bool expanded = static_cast<int>(slot) == activeSlot && selected != nullptr &&
			selected->slot == static_cast<int>(slot);
		for (const weaponres::Weapon* weapon : bySlot[slot]) {
			const bool active = selected == weapon;
			const float w = expanded ? kItemActiveW : kItemInactiveW;
			const float h = expanded ? kItemActiveH : kItemInactiveH;

			// Old UpdateColor: ammo state picks the scheme tint, rainbow
			// rotates the hue per item.
			unsigned char r = 0, g = 255, b = 0;
			unsigned char ri = 0, gi = 125, bi = 0;
			if (!weaponres::hasAmmo(weapon)) {
				r = 255, g = 0;
				ri = 125, gi = 0;
			}
			if (weaponchoose::RainbowMenu && weaponchoose::RainbowMenu->value > 0) {
				rainbowShift(RainbowCounter, r, g, b);
				rainbowShift(RainbowCounter, ri, gi, bi);
			}

			// Active sprite while highlighted, inactive otherwise; the
			// ammo bar pair rides the top (old item PerformLayout).
			const weaponres::Weapon::SpriteSource& source = active ? weapon->active : weapon->inactive;
			std::string img;
			if (source.valid) {
				sprintf_s(buffer,
					"<img src='spricon://%s/%d,%d,%d,%d' style='width:100%%; height:100%%; image-color: rgba(%d, %d, %d, %d);'/>",
					source.atlas.c_str(), source.rect.left, source.rect.top,
					source.rect.right, source.rect.bottom,
					active ? r : ri, active ? g : gi, active ? b : bi, active ? 255 : 125);
				img = buffer;
			}

			sprintf_s(buffer,
				"<div class='item%s' style='left:%.0fdp; top:%.0fdp; width:%.0fdp; height:%.0fdp;'>%s</div>",
				active ? " active" : "", x, y, w, h, img.c_str());
			rml += buffer;
			y += h + kGap;
			RainbowCounter++;
		}
		maxHeight = std::max(maxHeight, y);
		x += (expanded ? kItemActiveW : kItemInactiveW) + kGap;
	}

	// Bucket frame over the highlighted item (old select_icon ImagePanel).
	if (selected) {
		float bx = 0.0f;
		for (int slot = 0; slot < selected->slot; slot++) {
			bx += (slot == activeSlot ? kItemActiveW : kItemInactiveW) + kGap;
		}
		sprintf_s(buffer,
			"<img id='bucket' src='../img/weaponchoose/select_icon.tga' style='left:%.0fdp; top:%.0fdp; width:%.0fdp; height:%.0fdp;'/>",
			bx, kSlotIcon + kGap, kItemActiveW, kItemActiveH);
		rml += buffer;
	}

	if (Rml::Element* content = element("content")) {
		content->SetInnerRML(rml);
		content->SetProperty("width", std::to_string(x) + "dp");
		content->SetProperty("height", std::to_string(maxHeight) + "dp");
	}
}

void WeaponChoosePanel::confirmSelection() {
	// Old SelectWeapon: the highlighted pick must be usable (ammo or
	// SELECTONEMPTY); confirm sends the weapon command + select sound and
	// closes the bar.
	const weaponres::Weapon* weapon = weaponres::selectedWeapon();
	if (!weapon || !Visible)
		return;
	if (!(weapon->flags & weaponres::kItemFlagSelectOnEmpty) && !weaponres::hasAmmo(weapon))
		return;
	char command[128];
	sprintf_s(command, "%s\n", weapon->name.c_str());
	gEngfuncs.pfnClientCmd(command);
	gEngfuncs.pfnPlaySoundByName("common/wpn_select.wav", 1.0f);
	setVisible(false);
	SelectBlock = true;
}

bool WeaponChoosePanel::blockAttackOnce() {
	if (!SelectBlock)
		return false;
	SelectBlock = false;
	confirmSelection();
	return true;
}

void WeaponChoosePanel::onDocumentReady() {
	subscribe(weaponres::ChooseWeapon, [this](const weaponres::Weapon* weapon) {
		// Old ChooseWeapon: hudon sound on the first open of a selection
		// round, moveselect on each highlight move; the bar closes after
		// close_time without input.
		if (!shouldDraw())
			return;
		const bool firstOpen = !Visible;
		if (!weapon || !weaponres::hasWeapon(weapon->slot, weapon->slotPos)) {
			// Highlight walked off an owned weapon: empty slot - close.
			setVisible(false);
			return;
		}
		if (firstOpen) {
			gEngfuncs.pfnPlaySoundByName("common/wpn_hudon.wav", 1.0f);
			setVisible(true);
			rebuild();
		} else {
			gEngfuncs.pfnPlaySoundByName("common/wpn_moveselect.wav", 1.0f);
			rebuild();
		}
		CloseAtTime = ClientTimeNow + kCloseTime;
	});
	subscribe(weaponres::DropAllWeapons, [this]() {
		setVisible(false);
	});
	subscribe(weaponres::DropWeapon, [this](const weaponres::Weapon*) {
		if (Visible)
			rebuild();
	});
	subscribe(weaponres::PickupWeapon, [this](const weaponres::Weapon*) {
		if (Visible)
			rebuild();
	});
	setVisible(false);
}

void WeaponChoosePanel::onData(double frameTime) {
	ClientTimeNow += frameTime;
	if (!Document)
		return;
	// Old OnThink: the bar closes itself after close_time (re-armed on every
	// ChooseWeapon through the rebuild path).
	if (Visible && CloseAtTime > 0.0 && ClientTimeNow >= CloseAtTime) {
		CloseAtTime = 0.0;
		setVisible(false);
	}
}

}
