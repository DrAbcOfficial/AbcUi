#pragma once

// panels::AmmoBarPanel - current weapon ammo readout (icons + counts).
// Ported from ABCEnchance src/hud/vgui/ammobar.* (old AmmoPanel.res layout).
// Visibility: suit, weapons hide bit clear, weapon present and carrying at
// least one ammo type (old RefreshAmmo rules).

#include "../../framework/HudPanel.h"

namespace weaponres {
struct Weapon;
}

namespace panels {

class AmmoBarPanel : public framework::HudPanel {
public:
	AmmoBarPanel();

protected:
	void onDocumentReady() override;
	void reset() override;

private:
	void updateVisibility();
	void refreshAmmo();
	const weaponres::Weapon* HandledWeapon = nullptr;
};

}
