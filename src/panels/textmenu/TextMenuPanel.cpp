// panels::TextMenuPanel implementation - see TextMenuPanel.h.
// from: ABCEnchance/src/hud/vgui/textmenu.* (GBK->UTF-8).
#include <metahook.h>

#include "TextMenuPanel.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>

#include <abc/CvarCmd.h>
#include <abc/Events.h>

#include "../../data/NetworkMessages.h"

namespace panels {

namespace textmenu {

namespace {
// CChainedCommand instances are fixed-slot (abc/Events.h) and must hook at
// HUD_Init; they live for the DLL lifetime. The panel subscribes their
// events from onDocumentReady - unsubscribed states chain straight through
// (RunChain with no subscriber = original command).
std::unique_ptr<abc::CChainedCommand> Commands[10];
} // namespace

void registerCommands() {
	static bool registered = false;
	if (registered)
		return;
	registered = true;
	for (int i = 0; i < 10; i++) {
		char name[16];
		sprintf_s(name, "slot%d", i + 1);
		Commands[i] = std::make_unique<abc::CChainedCommand>(name);
	}
}

abc::CChainedCommand* command(int index) {
	return (index >= 0 && index < 10) ? Commands[index].get() : nullptr;
}

} // namespace textmenu

namespace {
// Old StartFade sequences TextMenuIn/Out: 0.2s alpha, visibility flips.
void fadeTo(Rml::Element* body, bool state) {
	if (!body)
		return;
	body->SetProperty("transition", "opacity 0.2s linear-in");
	body->SetProperty("opacity", state ? "1" : "0");
}

std::string escapeXml(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default: out += c;
		}
	}
	return out;
}
} // namespace

TextMenuPanel::TextMenuPanel()
	: HudPanel("textmenu", "abcui/rml/textmenu.rml") {}

void TextMenuPanel::onDocumentReady() {
	// Invisible until a ShowMenu arrives (opacity 0, not display:none - the
	// fade-out transition needs the element alive).
	if (Rml::Element* body = getDocument())
		fadeTo(body, false);

	subscribe(netmsg::ShowMenu, [this](int validSlots, int displaySeconds, int bits, const std::string& text) {
		showMenu(validSlots, displaySeconds, bits, text);
	});
	subscribe(netmsg::InitHud, [this]() {
		reset();
	});
	// Old g_EventCmdSlot handler: the menu eats slot keys while open; the
	// weapon-slot chain only runs when it is closed.
	for (int i = 0; i < 10; i++) {
		if (abc::CChainedCommand* chained = textmenu::command(i)) {
			subscribe(chained->Event(), [this, i]() {
				if (!isVisible())
					return true;
				selectMenuItem(i + 1);
				return false;
			});
		}
	}
}

void TextMenuPanel::showMenu(int validSlots, int displaySeconds, int bits, const std::string& text) {
	// Old MsgShowMenu.
	if (!WaitingForMore)
		MenuString.clear();
	ValidSlots = validSlots;
	const int needMore = bits & 0x7F;
	IsASMenu = ((bits >> 7) & 1) != 0;
	ShutoffTime = displaySeconds > 0 ? ClientTimeNow + displaySeconds : -1.0;
	if (ValidSlots) {
		MenuString += text;
		if (needMore == 0) {
			// Old: strip the leading/trailing newline runs servers send.
			const auto isContent = [](char ch) { return ch != '\n'; };
			const auto begin = std::find_if(MenuString.begin(), MenuString.end(), isContent);
			const auto rend = std::find_if(MenuString.rbegin(), MenuString.rend(), isContent);
			MenuString.erase(MenuString.begin(), begin);
			MenuString.erase(rend.base(), MenuString.end());
			if (Rml::Element* menu = element("menu"))
				menu->SetInnerRML(escapeXml(MenuString));
			if (Rml::Element* body = getDocument())
				fadeTo(body, true);
			show();
		}
	}
	else {
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
	}
	WaitingForMore = needMore != 0;
}

void TextMenuPanel::selectMenuItem(int slot) {
	// Old SelectMenuItem.
	char command[32];
	if (slot > 0 && (ValidSlots & (1 << (slot - 1)))) {
		sprintf_s(command, IsASMenu ? "as_menuselect %d\n" : "menuselect %d\n", slot);
		abc::EngineClientCmd(command);
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
	}
}

void TextMenuPanel::onData(double frameTime) {
	ClientTimeNow += frameTime;
	// Old OnThink shutoff.
	if (ShutoffTime >= 0.0 && ClientTimeNow >= ShutoffTime) {
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
		ShutoffTime = -1.0;
	}
}

void TextMenuPanel::reset() {
	// Old Reset.
	MenuString.clear();
	ShutoffTime = -1.0;
	WaitingForMore = false;
	IsASMenu = false;
	ValidSlots = 0;
	if (Rml::Element* menu = element("menu"))
		menu->SetInnerRML("");
	if (Rml::Element* body = getDocument())
		fadeTo(body, false);
	hide();
}

}
