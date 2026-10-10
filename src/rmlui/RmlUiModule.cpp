// rmlui::RmlUiModule implementation - see RmlUiModule.h.
#include <metahook.h>

#include "RmlUiModule.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <abc/CvarCmd.h>
#include <abc/Console.h>
#include <abc/Plugin.h>
#include <abc/Render.h>

#include "../framework/Assets.h"
#include "../framework/Localization.h"
#include "../framework/PanelManager.h"
#include "../framework/Theme.h"
#include "../data/HudState.h"
#include "../data/SpriteResource.h"

#include "AbcRenderInterface.h"
#include "AbcSystemInterface.h"
#include "InputBridge.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {

// System fonts, loaded straight from the Windows font dir (plan 05 §3.4 -
// no redistribution). TTC multi-face support is a Phase 3 verification item
// (R-10): failures warn per-face; if nothing loads, text stays invisible but
// the plugin keeps running. Verdana/Tahoma cover the old scheme's latin
// faces; YaHei is the CJK fallback (per-glyph fallback through the RCSS
// font-family list).
constexpr const char* kFontPaths[] = {
	"C:\\Windows\\Fonts\\verdana.ttf",  // old scheme body/number face
	"C:\\Windows\\Fonts\\verdanab.ttf", // Verdana bold (hud numbers)
	"C:\\Windows\\Fonts\\tahoma.ttf",   // old "UiBold" face
	"C:\\Windows\\Fonts\\msyh.ttc",     // Microsoft YaHei regular (fallback face)
	"C:\\Windows\\Fonts\\msyhbd.ttc",   // Microsoft YaHei bold
	"C:\\Windows\\Fonts\\consola.ttf",  // Consolas (fallback face)
};

// Context/renderer singletons (declared before RendererCallbacks: its
// renderFrame reads them).
Rml::Context* Context = nullptr;

class RendererCallbacks : public IMetaRendererCallbacks {
public:
	void OnBeginRendering(int*, int*, int*, int*) override {}
	void OnRenderFrameStart() override {}
	void OnBeginRenderingPost(int*, int*, int*, int*) override {}
	void OnFreeFrameBuffers() override {}
	void OnGenerateFrameBuffers() override {}
	void OnPreRenderView() override {}
	void OnPreRenderViewPost() override {}
	void OnPostRenderView() override {}
	void OnPostRenderViewPost() override {}
	void OnRenderEndFrame() override { renderFrame(); }

	// Render the Rml scene onto the final buffer, once per engine frame,
	// right before it is blitted to screen (see module header note).
	void renderFrame() {
		auto* renderer = abc::MetaRenderer();
		if (!renderer || !RenderInterface || !Context)
			return;
		renderer->PushFrameBuffer();
		renderer->BindFrameBuffer(renderer->GetFinalBufferFBO());
		RenderInterface->beginFrame();
		Context->Render();
		RenderInterface->endFrame();
		renderer->PopFrameBuffer();
	}

	std::unique_ptr<AbcRenderInterface> RenderInterface;
};

RendererCallbacks Callbacks;
std::unique_ptr<AbcSystemInterface> SystemInterface;
std::unique_ptr<AbcFileInterface> FileInterface;
bool Initialised = false;

void loadFonts() {
	int loaded = 0;
	for (const char* path : kFontPaths) {
		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
			continue;
		if (Rml::LoadFontFace(path, true))
			loaded++;
		else
			abc::ConsoleWarn("[AbcUi] font load failed: %s\n", path);
	}
	if (loaded == 0)
		abc::ConsoleWarn("[AbcUi] no system font loaded - text will not render\n");
}

void debuggerCommand() {
	const bool visible = Rml::Debugger::IsVisible();
	Rml::Debugger::SetVisible(!visible);
	abc::ConsolePrint("[AbcUi] debugger %s\n", !visible ? "enabled" : "disabled");
}

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
	// Also dump the live health panel's bar-wrap state: inline width value +
	// computed box - the decisive data for the "armor bar does not shrink"
	// report.
	if (Rml::ElementDocument* health = Context->GetDocument("health")) {
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
		if (Rml::ElementDocument* doc = Context->GetDocument(name))
			abc::ConsolePrint("[AbcUi] probe: doc %-14s visible=%d display='%s'\n", name,
				doc->IsVisible() ? 1 : 0, doc->GetProperty("display")->ToString().c_str());
	}
	if (Rml::ElementDocument* flashlight = Context->GetDocument("flashlight")) {
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
	ProbeDoc = Context->LoadDocumentFromMemory(rml);
	if (ProbeDoc) {
		ProbeDoc->Show();
		abc::ConsolePrint("[AbcUi] probe shown\n");
	}
	else {
		abc::ConsoleWarn("[AbcUi] probe document failed to load\n");
	}
}

} // namespace

void init() {
	if (Initialised)
		return;

	vgui2ext::init();

	SystemInterface = std::make_unique<AbcSystemInterface>();
	FileInterface = std::make_unique<AbcFileInterface>();
	Callbacks.RenderInterface = std::make_unique<AbcRenderInterface>();
	Rml::SetSystemInterface(SystemInterface.get());
	Rml::SetFileInterface(FileInterface.get());
	Rml::SetRenderInterface(Callbacks.RenderInterface.get());
	if (!Rml::Initialise()) {
		abc::SysError("[AbcUi] Rml::Initialise failed");
		return;
	}

	const auto width = static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth());
	const auto height = static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight());
	Context = Rml::CreateContext("abcui", Rml::Vector2i(width, height), Callbacks.RenderInterface.get());
	if (!Context) {
		abc::SysError("[AbcUi] Rml::CreateContext failed");
		return;
	}

	loadFonts();
	framework::Localization::load();
	framework::Theme::applyScale(Context);
	sprites::vidInit();
	framework::Assets::verifyInstalled();
	framework::PanelManager::initAll(Context);

	Rml::Debugger::Initialise(Context);
	Rml::Debugger::SetVisible(false);

	abc::MetaRenderer()->RegisterRenderCallbacks(&Callbacks);
	Initialised = true;
	abc::ConsolePrint("[AbcUi] RmlUi %s ready (context %dx%d)\n", Rml::GetVersion().c_str(), width, height);
}

void shutdown() {
	if (!Initialised)
		return;
	Initialised = false;
	if (auto* renderer = abc::MetaRenderer())
		renderer->UnregisterRenderCallbacks(&Callbacks);
	framework::PanelManager::shutdown();
	sprites::shutdown();
	// Sprite textures died with the bulk delete above; drop the additive
	// marks so recycled texture ids stay on the regular alpha program.
	Callbacks.RenderInterface->clearAdditiveTextures();
	Context = nullptr;
	Rml::Shutdown();
	Callbacks.RenderInterface.reset();
	FileInterface.reset();
	SystemInterface.reset();
	vgui2ext::shutdown();
}

void registerCommands() {
	abc::ADD_COMMAND("abc_ui_debugger", debuggerCommand);
	abc::ADD_COMMAND("abc_ui_probe", probeCommand);
}

void onFrame(double frameTime) {
	// Deferred assembly (see module header): HUD_VidInit proved too
	// early-bound (-windowed initializes video before the export patch
	// lands), so the first HUD_Frame carries the one-shot init.
	if (!Initialised) {
		init();
		if (!Initialised || !Context)
			return;
		framework::Theme::applyScale(Context);
		Context->SetDimensions(Rml::Vector2i(static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()),
			static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight())));
	}
	InputBridge::update(Context);
	framework::PanelManager::onFrame(frameTime);
	Context->Update();
}

void onVidInit() {
	if (!Initialised)
		return;
	framework::Theme::applyScale(Context);
	Context->SetDimensions(Rml::Vector2i(static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()),
		static_cast<int>(abc::MetaRenderer()->GetSwapChainHeight())));
	framework::PanelManager::vidInitAll();
}

void onKeyEvent(int down, int keynum) {
	if (!Initialised || !Context)
		return;
	InputBridge::onKeyEvent(Context, down, keynum);
}

void onHudShutdown() {
	shutdown();
}

}
