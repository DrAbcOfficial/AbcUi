#pragma once

// Shared descriptor/document plumbing for the HUD panel modules. The module
// still owns its callbacks, its panel state and its document; only the field
// fill order (which the host validates) and the null-safe document lookup
// live here.

#include <abcui/module_api.h>

namespace moduleui {

// Fills the descriptor the host expects. The module assigns userdata and the
// callbacks afterwards.
inline AbcUiPanelDesc makePanel(const char *name, const char *documentPath,
                                int32_t zIndex = 0) {
  AbcUiPanelDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = name;
  desc.documentPath = documentPath;
  desc.zIndex = zIndex;
  return desc;
}

inline AbcUiDocument *panelDocument(const AbcUiModuleApi *api,
                                    const char *name) {
  return api && api->ui ? api->ui->panelDocument(name) : nullptr;
}

} // namespace moduleui
