// Per-plugin metahook globals. AbcCore.dll keeps its own copy; CAbcPluginBase
// copies these host pointers into the shared library through the exported
// setters during Init/LoadEngine/LoadClient.
#include <metahook.h>

mh_interface_t *g_pInterface = nullptr;
cl_enginefunc_t gEngfuncs = {};
metahook_api_t *g_pMetaHookAPI = nullptr;
mh_enginesave_t *g_pMetaSave = nullptr;
cl_exportfuncs_t gExportfuncs = {};
