#include <metahook.h>

// AbcUi's own MetaHook globals. CAbcPluginBase copies the host pointers into
// AbcCore.dll, while plugin code keeps using these DLL-local copies.
mh_interface_t* g_pInterface = nullptr;
cl_enginefunc_t gEngfuncs = {};
metahook_api_t* g_pMetaHookAPI = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
cl_exportfuncs_t gExportfuncs = {};
