// abcui.gameui - the replacement game-menu pages, one document per page.
// The entry validates the framework's table once and fans the module
// lifecycle out to the page projections (the vote creation menu and the
// player list so far; the shell and the remaining pages follow in their own
// commits). A page owns its DOM, its local gestures and its model-section
// calls; the framework keeps the models, the seams and the navigation
// state machine.
#include "PageShared.h"

namespace votepage {
void load();
void shutdown();
void unload();
} // namespace votepage

namespace playerlistpage {
void load();
void shutdown();
void unload();
} // namespace playerlistpage

namespace commandmenupage {
void load();
void shutdown();
void unload();
} // namespace commandmenupage

namespace menupage {
void load();
void shutdown();
void unload();
} // namespace menupage

namespace {

void onModuleLoad() {
  votepage::load();
  playerlistpage::load();
  commandmenupage::load();
  menupage::load();
}

void onModuleShutdown() {
  // The documents die with the context; every page forgets its handles.
  votepage::shutdown();
  playerlistpage::shutdown();
  commandmenupage::shutdown();
  menupage::shutdown();
}

void onModuleUnload() {
  votepage::unload();
  playerlistpage::unload();
  commandmenupage::unload();
  menupage::unload();
  Api = nullptr;
}

} // namespace

ABCUI_MODULE_EXPORT int AbcUiModule_Main(uint32_t abiVersion,
                                         const AbcUiModuleApi *api,
                                         AbcUiModuleDescriptor *out) {
  if (abiVersion != ABCUI_ABI_VERSION || !api || !out)
    return 0;
  // Every section the pages act through (see PageShared.h and the page
  // sources for which calls each one makes).
  if (api->structSize < sizeof(AbcUiModuleApi) || !api->core || !api->events ||
      !api->ui || !api->console || !api->documents || !api->menu ||
      !api->votemenu || !api->playerlist || !api->commandmenu ||
      !api->menuconfig || !api->social)
    return 0;
  if (api->ui->structSize < sizeof(AbcUiUiApi) ||
      api->documents->structSize < sizeof(AbcUiDocumentsApi) ||
      api->menu->structSize < sizeof(AbcUiMenuApi) ||
      api->votemenu->structSize < sizeof(AbcUiVoteMenuApi) ||
      api->playerlist->structSize < sizeof(AbcUiPlayerListApi) ||
      api->commandmenu->structSize < sizeof(AbcUiCommandMenuApi) ||
      api->menuconfig->structSize < sizeof(AbcUiMenuConfigApi) ||
      api->social->structSize < sizeof(AbcUiSocialApi))
    return 0;
  Api = api;

  out->structSize = sizeof(AbcUiModuleDescriptor);
  out->id = "abcui.gameui";
  out->version = "1.0.0";
  out->onLoad = &onModuleLoad;
  out->onInit = nullptr;
  out->onContextReady = nullptr;
  out->onFrame = nullptr;
  out->onVidInit = nullptr;
  out->onShutdown = &onModuleShutdown;
  out->onUnload = &onModuleUnload;
  return 1;
}
