// rmlui::ProbeCommand implementation - see ProbeCommand.h.
#include "ProbeCommand.h"

#ifndef NDEBUG

#include <metahook.h>

#include <RmlUi/Core.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include <keydefs.h>
#include <abc/Math.h>

#include "../data/HudState.h"
#include "../data/NetworkMessages.h"
#include "../data/PlayerResource.h"
#include "../panels/scoreboard/ScoreboardPanel.h"

#include "RmlUiModule.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {

// Input-reach echo for the acceptance driver: the driver moves the engine
// cursor / traps an engine key and asserts these lines on the stdout mirror
// (same instrument role the retired DemoPanel played in Phase 3, but
// Debug-builds-only and attached to the probe document).
class ProbeInputEcho : public Rml::EventListener {
public:
	void ProcessEvent(Rml::Event& event) override {
		if (event.GetId() == Rml::EventId::Keydown) {
			abc::ConsolePrint("[AbcUi] probe key id=%d\n",
				event.GetParameter<int>("key_identifier", 0));
			return;
		}
		abc::ConsolePrint("[AbcUi] probe mouse %d,%d\n",
			event.GetParameter<int>("mouse_x", 0),
			event.GetParameter<int>("mouse_y", 0));
	}
};
ProbeInputEcho InputEcho;

// Live font-pipeline probe (batch A acceptance aid): builds a document from
// an inline sheet - three explicit font families plus one line styled only
// through the file-based base.rcss. Which lines render localizes a
// missing-text report (face lookup vs base.rcss vs document markup).
void probeCommand() {
	static Rml::ElementDocument* ProbeDoc = nullptr;
	if (ProbeDoc) {
		ProbeDoc->Close();
		ProbeDoc = nullptr;
		abc::ConsolePrint("[AbcUi] probe closed\n");
		return;
	}
	Rml::Context* context = rmlui::context();
	if (!context)
		return;
	// Also dump the live health panel's bar-wrap state: inline width value +
	// computed box - the decisive data for the "armor bar does not shrink"
	// report.
	if (Rml::ElementDocument* health = context->GetDocument("health")) {
		if (Rml::Element* wrap = health->GetElementById("armor-bar-wrap")) {
			const Rml::Box box = wrap->GetBox();
			const Rml::Vector2f content = box.GetSize();
			abc::ConsolePrint("[AbcUi] probe: armor-bar-wrap inline width='%s' computed=%.2fx%.2f\n",
				wrap->GetProperty("width")->ToString().c_str(),
				content.x, content.y);
		}
		else {
			abc::ConsoleWarn("[AbcUi] probe: armor-bar-wrap not found\n");
		}
	}
	abc::ConsolePrint("[AbcUi] probe: hudstate suit=%d hideFlash=%d tokens=%d spectate=%d\n",
		hudstate::hasSuit() ? 1 : 0,
		hudstate::isHudHidden(hudstate::kHideFlashlight) ? 1 : 0,
		hudstate::debugHideTokens(),
		hudstate::isSpectate() ? 1 : 0);
	// Keydown follows the context focus; print who owns it so a stuck input
	// report localizes in one probe run.
	if (Rml::Element* focus = context->GetFocusElement()) {
		Rml::ElementDocument* owner = focus->GetOwnerDocument();
		abc::ConsolePrint("[AbcUi] probe: focus doc id='%s' tag='%s' (element tag='%s' id='%s')\n",
			owner ? owner->GetId().c_str() : "<none>",
			owner ? owner->GetTagName().c_str() : "",
			focus->GetTagName().c_str(), focus->GetId().c_str());
	}
	else {
		abc::ConsoleWarn("[AbcUi] probe: no focus element\n");
	}
	for (const char* name : {"health", "flashlight", "ammobar", "ammostack", "itemstack",
			 "weaponstack", "deadmsg", "notice", "notice-center", "sidetext", "textmenu",
			 "crosshair", "indicator", "gindicator", "dmgtiles", "effect", "popnum"}) {
		if (Rml::ElementDocument* doc = context->GetDocument(name))
			abc::ConsolePrint("[AbcUi] probe: doc %-14s visible=%d display='%s'\n", name,
				doc->IsVisible() ? 1 : 0, doc->GetProperty("display")->ToString().c_str());
	}
	if (Rml::ElementDocument* flashlight = context->GetDocument("flashlight")) {
		if (Rml::Element* off = flashlight->GetElementById("off-image")) {
			const Rml::Box box = off->GetBox();
			const Rml::Vector2f content = box.GetSize();
			abc::ConsolePrint("[AbcUi] probe: flashlight off-image src='%s' visibility='%s' computed=%.2fx%.2f\n",
				off->GetAttribute<Rml::String>("src", "<none>").c_str(),
				off->GetProperty("visibility")->ToString().c_str(),
				content.x, content.y);
		}
	}
	// Batch B ground truth: the popnum doc's live children with their inline
	// and absolute positions (projection chain: netmsg -> element -> layout).
	if (Rml::ElementDocument* popnum = context->GetDocument("popnum")) {
		if (Rml::Element* numbers = popnum->GetElementById("numbers")) {
			int count = numbers->GetNumChildren();
			abc::ConsolePrint("[AbcUi] probe: popnum children=%d doc-visible=%d\n",
				count, popnum->IsVisible() ? 1 : 0);
			for (int i = 0; i < count && i < 4; i++) {
				Rml::Element* child = numbers->GetChild(i);
				abc::ConsolePrint("[AbcUi] probe: popnum[%d] text='%s' inline-left='%s' inline-top='%s' abs=(%.1f %.1f)\n",
					i, child->GetInnerRML().c_str(),
					child->GetProperty("left")->ToString().c_str(),
					child->GetProperty("top")->ToString().c_str(),
					child->GetAbsoluteLeft(), child->GetAbsoluteTop());
			}
		}
	}
	// GIndicator ground truth: tracked-marker count + positions.
	if (Rml::ElementDocument* gind = context->GetDocument("gindicator")) {
		if (Rml::Element* markers = gind->GetElementById("markers")) {
			const int count = markers->GetNumChildren();
			abc::ConsolePrint("[AbcUi] probe: gindicator markers=%d\n", count);
			for (int i = 0; i < count && i < 4; i++) {
				Rml::Element* child = markers->GetChild(i);
				abc::ConsolePrint("[AbcUi] probe: gind[%d] display='%s' inline-left='%s' inline-top='%s' abs=(%.1f %.1f)\n",
					i, child->GetProperty("display")->ToString().c_str(),
					child->GetProperty("left")->ToString().c_str(),
					child->GetProperty("top")->ToString().c_str(),
					child->GetAbsoluteLeft(), child->GetAbsoluteTop());
			}
		}
	}
	// Batch C ground truth: the scoreboard box/table display state (the
	// +showscores chain) and the new documents' visibility.
	if (Rml::ElementDocument* board = context->GetDocument("scoreboard")) {
		if (Rml::Element* box = board->GetElementById("box")) {
			Rml::Element* table = box->GetElementById("table");
			abc::ConsolePrint("[AbcUi] probe: scoreboard box display='%s' opacity='%s' abs=(%.1f %.1f) table-children=%d doc-visible=%d\n",
				box->GetProperty("display")->ToString().c_str(),
				box->GetProperty("opacity")->ToString().c_str(),
				box->GetAbsoluteLeft(), box->GetAbsoluteTop(),
				table ? table->GetNumChildren() : -1,
				board->IsVisible() ? 1 : 0);
		}
	}
	for (const char* doc : {"playerboard", "vote", "motd", "weaponchoose"}) {
		if (Rml::ElementDocument* d = context->GetDocument(doc))
			abc::ConsolePrint("[AbcUi] probe: doc-batchc %s visible=%d\n", doc, d->IsVisible() ? 1 : 0);
	}
	// Mouse-chain diagnostics (P6 audit round): cursor state, the R-4 WndProc
	// callback registration and the scoreboard pointer mode (wantsWheel =
	// visible && pointer-mode).
	{
		int cx = -1, cy = -1;
		const bool cursor = vgui2ext::getCursorPos(cx, cy);
		abc::ConsolePrint("[AbcUi] probe: mouse cursor=%d visible=%d pos=(%d,%d) wndproc=%d owner=%d scoreboard-wheel=%d\n",
			cursor ? 1 : 0, vgui2ext::isCursorVisible() ? 1 : 0, cx, cy,
			vgui2ext::uiCallbacksRegistered() ? 1 : 0,
			rmlui::uiMouseOwner() ? 1 : 0,
			panels::scoreboard::wantsWheel() ? 1 : 0);
	}
	const char* rml =
		"<rml>"
		"<head>"
		"<link type=\"text/rcss\" href=\"abcui/rcss/base.rcss\"/>"
		"<style>"
		"	body { margin: 0dp; width: 640dp; height: 640dp;"
		"		background-color: #101418e0; }"
		"	div { display: block; }"
		"	#a { font-family: \"Verdana\"; font-size: 40px; color: #ff0000; }"
		"	#b { font-family: \"Microsoft YaHei\"; font-size: 40px; color: #00ff00; }"
		"	#c { font-size: 40px; color: #ffff00; }"
		"</style>"
		"</head>"
		"<body>"
		"<div id=\"a\">PROBE-VERDANA-123</div>"
		"<div id=\"b\">PROBE-YAHEI-ABC abc \xe6\xb5\x8b\xe8\xaf\x95</div>"
		"<div id=\"c\">PROBE-UNSPECIFIED-456</div>"
		"<div id=\"d\" class=\"font-default\">PROBE-BASECSS-789</div>"
		"</body>"
		"</rml>";
	ProbeDoc = context->LoadDocumentFromMemory(rml);
	if (ProbeDoc) {
		ProbeDoc->Show();
		// Keydown follows the focus chain; the doc must own focus for the
		// echo to fire when the engine traps a key in-map.
		ProbeDoc->Focus();
		ProbeDoc->AddEventListener(Rml::EventId::Mousemove, &InputEcho);
		ProbeDoc->AddEventListener(Rml::EventId::Keydown, &InputEcho);
		if (Rml::Element* focus = context->GetFocusElement())
			abc::ConsolePrint("[AbcUi] probe: post-open focus tag='%s' id='%s' doc='%s'\n",
				focus->GetTagName().c_str(), focus->GetId().c_str(),
				ProbeDoc->Contains(focus) ? "probe" : "other");
		else
			abc::ConsoleWarn("[AbcUi] probe: post-open focus is null\n");
		abc::ConsolePrint("[AbcUi] probe shown\n");
	}
		else {
			abc::ConsoleWarn("[AbcUi] probe document failed to load\n");
		}
	}

	// ShowMenu injector for the acceptance driver: fires the exact event the
	// wire parser emits (netmsg::ShowMenu), so a session without a live server
	// menu stream can still exercise the real downstream chain - TextMenu
	// open/fade, the slot1..10 chained-command gate, the menuselect emission
	// and the re-show-cancels-pending-hide path. The canned text carries the
	// TextImageEx colour stream the panel must translate.
	void menuCommand() {
		Rml::Context* context = rmlui::context();
		if (!context)
			return;
		netmsg::ShowMenu(0x7, 15, 0,
			"\\r[AbcUi]\\w Menu probe\n"
			"\n"
			"\\y1. Alpha option\n"
			"\\w2. Beta option\n"
			"\\d3. Gamma disabled\n");
		abc::ConsolePrint("[AbcUi] menu: injected ShowMenu (slots 1-3, 15s, plain menuselect)\n");
	}

	// Batch B feedback injector: fires the wire-parser events the feedback
	// panels subscribe (Damage / Health / PopNum) with a source point 100u
	// ahead of the view, so Indicator + DmgTiles + Effect(danger) + PopNum
	// run their real downstream chains without a live attacker. GIndicator
	// needs a live grenade entity and stays a real-game check.
	void feedbackCommand() {
		Rml::Context* context = rmlui::context();
		if (!context)
			return;
		const cl_entity_t* local = gEngfuncs.GetLocalPlayer();
		if (!local) {
			abc::ConsoleWarn("[AbcUi] feedback: no local player\n");
			return;
		}
		vec3_t angles{}, forward{};
		gEngfuncs.GetViewAngles(angles);
		abc::CMathlib::AngleVectors(angles, forward, nullptr, nullptr);

		netmsg::DamageData damage;
		damage.armor = 25;
		damage.damage = 40;
		damage.tiles = (1 << 17) | (1 << 3) | (1 << 8); // POISON | BURN | SHOCK
		for (int i = 0; i < 3; i++)
			damage.from[i] = local->curstate.origin[i] + forward[i] * 100.0f;
		damage.from[2] += 8.0f;
		netmsg::Damage(damage);

		netmsg::Health(20);

		netmsg::PopNumData popnum;
		for (int i = 0; i < 3; i++)
			popnum.origin[i] = local->curstate.origin[i] + forward[i] * 80.0f;
		popnum.origin[2] += 16.0f;
		popnum.value = 12345;
		popnum.color[0] = 255;
		popnum.color[1] = 200;
		popnum.color[2] = 50;
		popnum.color[3] = 255;
		netmsg::PopNum(popnum);

		abc::ConsolePrint("[AbcUi] feedback: injected Damage(25/40 poison+burn+shock) + Health(20) + PopNum(12345)\n");
	}

	// Batch C full-occupancy board instrument: pins 31 fake playerres slots
	// (mixed teams/health/donor/admin) so a local session can drive the
	// scoreboard table/scroll/context-menu at 32 occupancy. The wire-parse
	// layer stays untouched - this feeds the same store the real ScoreInfo
	// events write.
	void boardCommand() {
		playerres::debugPopulateBoard();
		abc::ConsolePrint("[AbcUi] board: 31 fake players pinned; +showscores opens, MOUSE2 pointer, right-click rows\n");
	}

	// Injected mouse keys (MOUSE2/wheel) never reach the client key chain
	// (P6 acceptance finding - real hardware does), so the driver flips the
	// pointer mode and pumps the wheel directly.
	void pointerOnCommand() {
		panels::scoreboard::setPointerMode(true);
		abc::ConsolePrint("[AbcUi] pointer: on\n");
	}

	void pointerOffCommand() {
		panels::scoreboard::setPointerMode(false);
		abc::ConsolePrint("[AbcUi] pointer: off\n");
	}

	void wheelUpCommand() {
		rmlui::onWheel(1, K_MWHEELUP);
		abc::ConsolePrint("[AbcUi] wheel: up\n");
	}

	void wheelDownCommand() {
		rmlui::onWheel(1, K_MWHEELDOWN);
		abc::ConsolePrint("[AbcUi] wheel: down\n");
	}


} // namespace

void registerProbeCommand() {
	abc::ADD_COMMAND("abc_ui_probe", probeCommand);
	abc::ADD_COMMAND("abc_ui_menu", menuCommand);
	abc::ADD_COMMAND("abc_ui_feedback", feedbackCommand);
	abc::ADD_COMMAND("abc_ui_board", boardCommand);
	abc::ADD_COMMAND("abc_ui_pointer_on", pointerOnCommand);
	abc::ADD_COMMAND("abc_ui_pointer_off", pointerOffCommand);
	abc::ADD_COMMAND("abc_ui_wheelup", wheelUpCommand);
	abc::ADD_COMMAND("abc_ui_wheeldown", wheelDownCommand);
}

}

#else // NDEBUG

namespace rmlui {

void registerProbeCommand() {
}

}

#endif // NDEBUG
