// AbcUi - RmlUi HUD plugin (plan 05). Plugin entry only: CAbcPluginBase
// derivation + EXPOSE_SINGLE_INTERFACE. All assembly lives in rmlui/ (RmlUi
// integration) and framework/ (panel framework).
//
// The metahook globals (g_pInterface/gEngfuncs/g_pMetaHookAPI/g_pMetaSave/
// gExportfuncs) are DEFINED by abc_core's Plugin.cpp - one copy per plugin DLL
// (static lib). Do not define them here (duplicate symbol, see README).
#include <metahook.h>

#include <abc/Console.h>
#include <abc/Plugin.h>
#include <abc/Render.h>

#include "hooks.h"
#include "rmlui/RmlUiModule.h"
#include "rmlui/SteamAPI.h"
#include "rmlui/VGUI2ExtensionImport.h"

class CAbcUi : public abc::CAbcPluginBase {
public:
	CAbcUi() : abc::CAbcPluginBase("AbcUi", "0.6.0") {}

protected:
	void OnLoadClient(cl_exportfuncs_t* pExportFuncs) override {
		// Fatal Renderer check + interface grab (plan 05 §2 startup order).
		// Rml assembly itself defers to the first HUD_Frame (see
		// RmlUiModule.h): the VGUI surface texture channel the font engine
		// needs is not usable this early (Phase 3 trace finding).
		abc::MetaRendererInit();
		vgui2ext::init();
		steamapi::init();
		hooks::locateVanillaHudList();
		hooks::locateNativeHudPanelInfo();
		hooks::locatePlayerColor();
		hooks::attachExports(pExportFuncs);
		abc::ConsolePrint("[AbcUi] loaded %s\n", Version());
	}

	void OnShutdown() override {
		rmlui::shutdown();
		steamapi::shutdown();
		vgui2ext::shutdown();
	}
};

EXPOSE_SINGLE_INTERFACE(CAbcUi, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
