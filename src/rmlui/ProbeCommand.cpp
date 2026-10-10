// rmlui::ProbeCommand implementation - see ProbeCommand.h.
#include "ProbeCommand.h"

#ifndef NDEBUG

#include <RmlUi/Core.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include "../data/HudState.h"
#include "../data/NetworkMessages.h"

#include "RmlUiModule.h"

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
			 "weaponstack", "deadmsg", "notice", "notice-center", "sidetext", "textmenu"}) {
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


} // namespace

void registerProbeCommand() {
	abc::ADD_COMMAND("abc_ui_probe", probeCommand);
	abc::ADD_COMMAND("abc_ui_menu", menuCommand);
}

}

#else // NDEBUG

namespace rmlui {

void registerProbeCommand() {
}

}

#endif // NDEBUG
