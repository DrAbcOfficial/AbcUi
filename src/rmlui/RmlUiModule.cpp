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
// the plugin keeps running.
constexpr const char* kFontPaths[] = {
	"C:\\Windows\\Fonts\\msyh.ttc",   // Microsoft YaHei regular (fallback face)
	"C:\\Windows\\Fonts\\msyhbd.ttc", // Microsoft YaHei bold
	"C:\\Windows\\Fonts\\consola.ttf", // Consolas (fallback face)
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
	Context = nullptr;
	Rml::Shutdown();
	Callbacks.RenderInterface.reset();
	FileInterface.reset();
	SystemInterface.reset();
	vgui2ext::shutdown();
}

void registerCommands() {
	abc::ADD_COMMAND("abc_ui_debugger", debuggerCommand);
}

void onFrame(double frameTime) {
	// Deferred assembly (see module header): HUD_VidInit proved too
	// early-bound (-windowed initializes video before the export patch
	// lands), so the first HUD_Frame carries the one-shot init.
	if (!Initialised) {
		init();
		if (!Initialised || !Context)
			return;
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
