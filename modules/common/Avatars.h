#pragma once

#include <abcui/module_api.h>

#include <string>

namespace moduleui {
inline std::string avatarUri(const AbcUiModuleApi *api, uint64_t steamId,
                             const char *fallback) {
  int32_t generation = 0;
  if (api->players->avatar(steamId, &generation) != ABCUI_OK)
    return fallback;
  return "avatar://" + std::to_string(steamId) +
         "?gen=" + std::to_string(generation);
}
} // namespace moduleui
