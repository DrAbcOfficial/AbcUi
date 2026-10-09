// AbcUi - plugin entry. Phase 1 skeleton on abc::CAbcPluginBase; the RmlUi
// framework (render/system interfaces, panels) lands in Phase 3 (05-abcui.md).
//
// The metahook globals (g_pInterface/gEngfuncs/g_pMetaHookAPI/g_pMetaSave/
// gExportfuncs) are DEFINED by abc_core's Plugin.cpp - one copy per plugin DLL
// (static lib). Do not define them here (duplicate symbol, see README).
#include <metahook.h>

#include <abc/Plugin.h>
#include <abc/Console.h>

class CAbcUi : public abc::CAbcPluginBase {
public:
	CAbcUi() : abc::CAbcPluginBase("AbcUi", "0.1.0") {}

protected:
	void OnLoadClient(cl_exportfuncs_t* pExportFuncs) override {
		// Phase 3: hook pExportFuncs->HUD_Frame/HUD_Redraw for the RmlUi pump.
		(void)pExportFuncs;
		abc::ConsolePrint("AbcUi loaded (%s)\n", Version());
	}
};

EXPOSE_SINGLE_INTERFACE(CAbcUi, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
