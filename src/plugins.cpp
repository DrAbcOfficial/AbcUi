#include <metahook.h>

#include <cstdio>
#include <cstring>

#include <abc/Core.h>

// Globals declared extern in metahook.h - one set per plugin DLL (static lib).
mh_interface_t *g_pInterface;
cl_enginefunc_t gEngfuncs;
metahook_api_t *g_pMetaHookAPI;
mh_enginesave_t *g_pMetaSave;

void IPluginsV4::Init(metahook_api_t *pAPI, mh_interface_t *pInterface, mh_enginesave_t *pSave)
{
	g_pInterface = pInterface;
	g_pMetaHookAPI = pAPI;
	g_pMetaSave = pSave;
}

void IPluginsV4::LoadEngine(cl_enginefunc_t *pEngineFuncs)
{
	memcpy(&gEngfuncs, pEngineFuncs, sizeof(gEngfuncs));
}

void IPluginsV4::LoadClient(cl_exportfuncs_t *pExportFuncs)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "AbcUi loaded (%s)\n", abc::CoreVersion());
	gEngfuncs.pfnConsolePrint(buf);
}

void IPluginsV4::Shutdown(void)
{
}

void IPluginsV4::ExitGame(int iResult)
{
}

const char *IPluginsV4::GetVersion(void)
{
	return "AbcUi|Version:0.1.0";
}

EXPOSE_SINGLE_INTERFACE(IPluginsV4, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
