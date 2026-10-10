// rmlui::ProbeCommand implementation - see ProbeCommand.h.
#include "ProbeCommand.h"

#ifndef NDEBUG

#include <RmlUi/Core.h>

#include <abc/Console.h>
#include <abc/CvarCmd.h>

#include "../data/HudState.h"

#include "RmlUiModule.h"

namespace rmlui {

namespace {

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
		"	body { margin: 0dp; }"
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
		abc::ConsolePrint("[AbcUi] probe shown\n");
	}
	else {
		abc::ConsoleWarn("[AbcUi] probe document failed to load\n");
	}
}

} // namespace

void registerProbeCommand() {
	abc::ADD_COMMAND("abc_ui_probe", probeCommand);
}

}

#else // NDEBUG

namespace rmlui {

void registerProbeCommand() {
}

}

#endif // NDEBUG
