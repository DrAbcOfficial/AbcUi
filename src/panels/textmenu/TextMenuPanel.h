#pragma once

// panels::TextMenuPanel - vanilla ShowMenu replacement (number-key menus).
// Ported from ABCEnchance src/hud/vgui/textmenu.*: chunked ShowMenu
// accumulation (bits&0x7f = more-chunks pending, bit7 = ammo-selection
// menu), shutoff countdown, and the slot1..10 key interception - when the
// menu is open the chained slot commands turn into menuselect (old
// g_EventCmdSlot handler, consumed = the engine weapon-slot path runs only
// while the menu is closed).

#include "../../framework/HudPanel.h"

namespace panels {

namespace textmenu {
// HUD_Init-time: hooks slot1..10 through abc::CChainedCommand (plan 08 §4.9).
void registerCommands();
// The hooked command for a slot index (0-based), null before registration.
abc::CChainedCommand* command(int index);
}

class TextMenuPanel : public framework::HudPanel {
public:
	TextMenuPanel();

protected:
	void onDocumentReady() override;
	void onData(double frameTime) override;
	void reset() override;

private:
	// Old SelectMenuItem: valid slot -> menuselect/as_menuselect + fade out.
	void selectMenuItem(int slot);
	void showMenu(int validSlots, int displaySeconds, int bits, const std::string& text);

	int ValidSlots = 0;
	bool IsASMenu = false;
	bool WaitingForMore = false;
	std::string MenuString;
	double ShutoffTime = -1.0;
	double ClientTimeNow = 0.0;
};

}
