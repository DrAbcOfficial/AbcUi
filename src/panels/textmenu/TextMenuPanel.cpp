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
constexpr double kFadeSeconds = 0.2;
void fadeTo(Rml::Element* body, bool state) {
	if (!body)
		return;
	body->SetProperty("transition", "opacity 0.2s linear-in");
	body->SetProperty("opacity", state ? "1" : "0");
}

// Old TextImageEx::Paint inline colour stream (SC 5.26 menu text): \w \d \y
// \r \c \g \m \o switch colour mid-string, \R toggles right-aligned flow,
// a newline resets to the label colour. Colours byte-exact from the old
// switch; \d is the label fg itself (TextMenu.TextColor #dedede).
const char* menuColorCode(char code) {
	switch (code) {
		case 'w': return "#ffffff";
		case 'd': return "#dedede";
		case 'y': return "#ffd240"; // 255 210 64
		case 'r': return "#d24000"; // 210 64 0
		case 'c': return "#00ffff";
		case 'g': return "#00ff00";
		case 'm': return "#ff00ff";
		case 'o': return "#ffa500"; // 255 165 0
		default: return nullptr;
	}
}

std::string colorizeMenuRml(const std::string& text) {
	std::string out;
	out.reserve(text.size() + 32);
	bool inSpan = false;
	const auto closeSpan = [&]() {
		if (inSpan) {
			out += "</span>";
			inSpan = false;
		}
	};
	for (size_t i = 0; i < text.size(); i++) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '\\' && i + 1 < text.size()) {
			if (const char* hex = menuColorCode(text[i + 1])) {
				closeSpan();
				out += "<span style=\"color: ";
				out += hex;
				out += "\">";
				inSpan = true;
				i++;
				continue;
			}
			if (text[i + 1] == 'R') {
				// Right-align flow control: no inline-markup equivalent in
				// Rml; consume so it does not print (server menus rarely
				// ship it - old Paint mirrored the x cursor).
				i++;
				continue;
			}
		}
		if (c == '\r' || c <= 8) {
			// Old Paint: CR and control chars <= 8 are skipped.
			continue;
		}
		if (c == '\n') {
			// Old Paint resets to the label colour per line.
			closeSpan();
			out += c;
			continue;
		}
		if (c == '&') {
			// "&&" draws one ampersand; a single & was the hotkey marker
			// (old code skipped it, the next char still drew).
			if (i + 1 < text.size() && text[i + 1] == '&') {
				out += "&amp;";
				i++;
			}
			continue;
		}
		switch (c) {
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default: out += c;
		}
	}
	closeSpan();
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
					if (!MenuOpen)
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
				menu->SetInnerRML(colorizeMenuRml(MenuString));
			if (Rml::Element* body = getDocument())
				fadeTo(body, true);
			show();
			MenuOpen = true;
			// A fresh menu cancels a pending fade-out-hide (old StartFade(true)
			// ran CancelAnimationsForPanel first): the server's answer to a
			// menuselect usually lands inside the 0.2s window, and letting the
			// stale deadline fire would close the just-opened submenu.
			HideAtTime = -1.0;
		}
	}
	else {
		MenuOpen = false;
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
		// The document stays up for the fade-out; hide it once the opacity
		// transition has finished (OnFadeOutDone).
		HideAtTime = ClientTimeNow + kFadeSeconds;
	}
	WaitingForMore = needMore != 0;
}

void TextMenuPanel::selectMenuItem(int slot) {
	// Old SelectMenuItem.
	char command[32];
	if (slot > 0 && (ValidSlots & (1 << (slot - 1)))) {
		sprintf_s(command, IsASMenu ? "as_menuselect %d\n" : "menuselect %d\n", slot);
		abc::EngineClientCmd(command);
		MenuOpen = false;
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
		HideAtTime = ClientTimeNow + kFadeSeconds;
	}
}

void TextMenuPanel::onData(double frameTime) {
	ClientTimeNow += frameTime;
	// Old OnThink shutoff.
	if (ShutoffTime >= 0.0 && ClientTimeNow >= ShutoffTime) {
		MenuOpen = false;
		if (Rml::Element* body = getDocument())
			fadeTo(body, false);
		HideAtTime = ClientTimeNow + kFadeSeconds;
		ShutoffTime = -1.0;
	}
	// Fade-out finished: take the document out of the layout (old
	// TextMenuOut SetVisible 0 at +0.2s). Doing it here - instead of
	// keeping the document permanently shown - is what keeps slot-key
	// chaining (and the old IsTextMenuOpen gates) honest.
	if (HideAtTime >= 0.0 && ClientTimeNow >= HideAtTime) {
		HideAtTime = -1.0;
		hide();
	}
}

void TextMenuPanel::reset() {
	// Old Reset.
	MenuString.clear();
	ShutoffTime = -1.0;
	WaitingForMore = false;
	IsASMenu = false;
	ValidSlots = 0;
	MenuOpen = false;
	HideAtTime = -1.0;
	if (Rml::Element* menu = element("menu"))
		menu->SetInnerRML("");
	if (Rml::Element* body = getDocument())
		fadeTo(body, false);
	hide();
}

}
