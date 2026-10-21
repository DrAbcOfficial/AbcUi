#pragma once

// sdk/abcui/module_api.h - the AbcUiModuleApi table and module descriptor.
//
// A module's DLL exports exactly one function:
//
//   ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
//                                            const AbcUiModuleApi *api,
//                                            AbcUiModuleDescriptor *out);
//
// It receives the framework service table and fills the descriptor. Return
// nonzero on success; a zero return (or an inconsistent descriptor) skips
// the module and everything that depends on it. Descriptor callbacks run on
// the engine main thread at the same lifecycle points the framework's
// built-in modules use (see AGENTS.md "UI module architecture").

#include <stdint.h>

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Packed semantic version: (major << 16) | (minor << 8) | patch.
#define ABCUI_VERSION_PACK(major, minor, patch)                                \
  (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

typedef uint64_t AbcUiSubscription; // 0 is never issued.

// Data-channel sink: the payload pointer is only valid for the call.
typedef void (*AbcUiEventSink)(void *userdata, const void *payload);
// Veto-channel sink: returning nonzero vetoes and stops the dispatch here.
typedef int (*AbcUiEventVeto)(void *userdata, const void *payload);

// Console command callback (no arguments; use engine argv through the
// console api when needed).
typedef void (*AbcUiCommandFn)(void *userdata);

// ---------------------------------------------------------------- core ----

typedef struct AbcUiCoreApi {
  uint32_t structSize;
  uint32_t frameworkVersion; // ABCUI_VERSION_PACK of the running framework
  const char *frameworkVersionString;
  // Console output through the framework channel ("[module]" prefix). The
  // printf-style formatting happens framework-side.
  void (*print)(const char *format, ...);
  void (*warn)(const char *format, ...);
  void (*error)(const char *format, ...);
} AbcUiCoreApi;

// ------------------------------------------------------------- console ----

typedef void *AbcUiCvarHandle; // opaque registered cvar

typedef struct AbcUiConsoleApi {
  uint32_t structSize;
  // Registers a cvar and returns its handle (null when the engine refused).
  // Cvars survive HUD_Shutdown cycles; register from onInit.
  AbcUiCvarHandle (*registerCvar)(const char *name, const char *value,
                                  int flags);
  float (*cvarValue)(AbcUiCvarHandle cvar, float fallback);
  // Copies the cvar string (at most outSize-1 bytes + terminator).
  void (*cvarString)(AbcUiCvarHandle cvar, char *out, uint32_t outSize,
                     const char *fallback);
  // Registers a console command. Registering the same name twice updates
  // the handler without touching the engine table.
  void (*registerCommand)(const char *name, AbcUiCommandFn fn, void *userdata);
} AbcUiConsoleApi;

// -------------------------------------------------------------- events ----

// Channels are lowercase dotted strings owned by the publishing subsystem
// (see sdk/abcui/events.h for the constants and their payload structs).
// Priority > 0 intercepts before the publisher's default 0, < 0 runs after;
// the same priority keeps subscription order. Subscribe from onLoad or
// onDocumentReady and unsubscribe before onUnload returns.
typedef struct AbcUiEventsApi {
  uint32_t structSize;
  AbcUiSubscription (*subscribe)(const char *channel, int priority,
                                 AbcUiEventSink sink, void *userdata);
  AbcUiSubscription (*subscribeVeto)(const char *channel, int priority,
                                     AbcUiEventVeto sink, void *userdata);
  void (*unsubscribe)(AbcUiSubscription subscription);
} AbcUiEventsApi;

// ----------------------------------------------------------------- vfs ----

typedef struct AbcUiFileView AbcUiFileView; // opaque opened package entry

typedef struct AbcUiVfsApi {
  uint32_t structSize;
  // Opens "path" inside the module's own package ("rml/health.rml"). The
  // view stays valid until viewFree or the module unloads.
  AbcUiFileView *(*open)(const char *moduleId, const char *path);
  const void *(*viewData)(AbcUiFileView *view);
  uint32_t (*viewSize)(AbcUiFileView *view);
  // Releases a view (no-op for zero-copy entries, frees extracted ones).
  void (*viewFree)(AbcUiFileView *view);
  int (*exists)(const char *moduleId, const char *path);
} AbcUiVfsApi;

// -------------------------------------------------------------- panels ----

// Registers a HUD panel: one Rml document fed by the framework frame pumps.
// documentPath is a mod:// uri inside the module package ("mod://<id>/rml/
// health.rml"). Registration is only legal from onLoad; the panel joins the
// next installHudPanels pass (the same HUD_Init cycle).
typedef struct AbcUiPanelDesc {
  uint32_t structSize;
  const char *name;         // unique panel name (debug lookup, cvar prefix)
  const char *documentPath; // mod:// uri of the panel's rml
  int zIndex;               // document paint order; 0 = default
  void *userdata;           // passed back to every callback
  void (*onDocumentReady)(void *userdata);
  void (*onData)(void *userdata, double frameTime);   // HUD_Frame pump
  void (*onRedraw)(void *userdata, double frameTime); // HUD_Redraw pump
  void (*reset)(void *userdata);                      // map transition
} AbcUiPanelDesc;

typedef struct AbcUiPanelsApi {
  uint32_t structSize;
  int (*registerPanel)(const AbcUiPanelDesc *desc);
} AbcUiPanelsApi;

// ------------------------------------------------- module descriptor ------

typedef struct AbcUiModuleDescriptor {
  uint32_t structSize;
  const char *id;      // must equal the manifest id
  const char *version; // informational
  // Called right after the entry returns: register panels here. The Rml
  // context does not exist yet - never touch the DOM.
  void (*onLoad)(void);
  // HUD_Init one-shot block: cvar/command registration only.
  void (*onInit)(void);
  // The Rml context exists; re-fired after every shutdown cycle because
  // module documents die with the context and must be rebuilt.
  void (*onContextReady)(void);
  // HUD_Frame, after the panel data pump, before the context update.
  void (*onFrame)(double frameTime);
  // HUD_VidInit, after the context resize.
  void (*onVidInit)(void);
  // While the context is still alive: release documents, listeners and
  // borrowed handles. Not re-fired until the next init/contextReady.
  void (*onShutdown)(void);
  // Process teardown: every service is gone; free everything else.
  void (*onUnload)(void);
} AbcUiModuleDescriptor;

// ---------------------------------------------------------------- table ---

typedef struct AbcUiModuleApi {
  uint32_t structSize; // sizeof(AbcUiModuleApi) of the running framework
  uint32_t abiVersion; // ABCUI_ABI_VERSION of the running framework
  const AbcUiCoreApi *core;
  const AbcUiConsoleApi *console;
  const AbcUiEventsApi *events;
  const AbcUiVfsApi *vfs;
  const AbcUiPanelsApi *panels;
} AbcUiModuleApi;

#ifdef __cplusplus
} // extern "C"
#endif
