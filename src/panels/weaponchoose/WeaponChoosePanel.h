#pragma once

// panels::WeaponChoose - the Ecco-style weapon selection bar (old
// CWeaponChoosePanel, from: ABCEnchance/src/hud/vgui/weaponchoose.*,
// GBK->UTF-8). Slot icons across the top; owned weapons under each slot;
// the selected slot expands and the bucket frame marks the highlight.
// Selection rides data::weaponres::selectSlot (slot keys / invnext /
// invprev through the command bus, TextMenu-open gated); +attack confirms
// the highlighted pick (old vgui mouse-press confirm - normal play has no
// vgui cursor, so the attack key is the honest equivalent; D-17 note).
//
// hud_fastswitch never shows the bar (the command already went out); the
// hud_rainbow_menu cycle rides the rebuild (old PerformLayout counter).

#include "../../framework/HudPanel.h"

namespace panels {

namespace weaponchoose {
// HUD_Init-time: subscribes slot/invnext/invprev/+attack on the command bus
// (the TextMenu-open gate lives here, next to the menu's own chain).
void registerCvars();
void registerCommands();
bool textmenuOpen();
bool PanelBlockAttack();
}

class WeaponChoosePanel : public framework::HudPanel {
public:
	WeaponChoosePanel();

	void reset() override;

protected:
	void onDocumentReady() override;
	void onData(double frameTime) override;

private:
	// Rebuilds the bar DOM from weaponres (old PerformLayout); keeps the
	// panel open on selection changes.
	void rebuild();
	bool shouldDraw() const;
	// Old SelectWeapon: confirm the highlight (ammo gate + select sound).
	void confirmSelection();
	// Old BlockAttackOnce: the +attack chain consumes the press that
	// confirms a pick (swallowed once).
	bool blockAttackOnce();
	void setVisible(bool visible);

	friend bool weaponchoose::PanelBlockAttack();

	double CloseAtTime = 0.0;
	double ClientTimeNow = 0.0;
	bool Visible = false;
	bool SelectBlock = false;
	int RainbowCounter = 0;
};

}
