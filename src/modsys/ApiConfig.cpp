// framework::ConfigApi - the sdk config section (module-scoped config.json
// sections). See ApiTable.h for how it joins the module table.
#include "ApiTable.h"
#include "ModuleResources.h"

#include <cstring>
#include <string>
#include <string_view>

#include <rapidjson/document.h>

#include "../services/UserConfig.h"
#include "../services/userconfig/ModuleConfig.h"

namespace framework {
namespace ConfigApi {
namespace {

namespace userconfig = services::userconfig;

// The stored section plus its NUL must fit the section bound.
bool fitsSection(const std::string &json) {
  return json.size() + 1 <= userconfig::kMaxModuleSectionBytes;
}

bool validPayload(const char *json, uint32_t jsonSize) {
  rapidjson::Document document;
  document.Parse<rapidjson::kParseValidateEncodingFlag |
                 rapidjson::kParseIterativeFlag>(json, jsonSize);
  return !document.HasParseError() && document.IsObject();
}

int32_t apiReadSection(const char *name, char *out, uint32_t outSize,
                       uint32_t *required) {
  if (required)
    *required = 0;
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting())
    return ABCUI_ERROR_FORBIDDEN;
  if (!name || !userconfig::validModuleSection(name))
    return ABCUI_ERROR_INVALID;

  std::string json;
  const std::string key = userconfig::moduleSectionKey(owner->id, name);
  switch (services::userconfig::readSection(key.c_str(), json)) {
  case services::userconfig::LoadStatus::Missing:
    return ABCUI_ERROR_NOT_FOUND;
  case services::userconfig::LoadStatus::Loaded:
    break;
  case services::userconfig::LoadStatus::Unreadable:
  default:
    return ABCUI_ERROR_UNAVAILABLE;
  }

  // An oversized stored section reports the same LIMIT as a short buffer, so
  // the caller can tell "allocate more" from "the section is over the bound".
  if (!fitsSection(json)) {
    if (required)
      *required = static_cast<uint32_t>(json.size() + 1);
    return ABCUI_ERROR_LIMIT;
  }
  const uint32_t capacity = static_cast<uint32_t>(json.size()) + 1;
  if (required)
    *required = capacity;
  if (!out || outSize < capacity)
    return ABCUI_ERROR_LIMIT;
  std::memcpy(out, json.c_str(), capacity);
  return ABCUI_OK;
}

int32_t apiWriteSection(const char *name, const char *json, uint32_t jsonSize) {
  const auto owner = module_resources::current();
  if (!owner || !module_resources::accepting())
    return ABCUI_ERROR_FORBIDDEN;
  if (!name || !userconfig::validModuleSection(name) || !json || jsonSize == 0)
    return ABCUI_ERROR_INVALID;
  if (jsonSize >= userconfig::kMaxModuleSectionBytes)
    return ABCUI_ERROR_LIMIT;
  // Validate before the store parses its own copy: an invalid payload must
  // not reach config.json, where it would make every section unreadable.
  if (!validPayload(json, jsonSize))
    return ABCUI_ERROR_INVALID;

  const std::string key = userconfig::moduleSectionKey(owner->id, name);
  if (!services::userconfig::writeSection(key.c_str(),
                                          std::string(json, jsonSize)))
    return ABCUI_ERROR_UNAVAILABLE;
  return ABCUI_OK;
}

// One initializer per member, in declaration order (see the console table
// note in ApiTable.cpp: a missing entry becomes a null pointer).
const AbcUiConfigApi kConfigApi = {
    sizeof(AbcUiConfigApi),
    apiReadSection,
    apiWriteSection,
};

} // namespace

const AbcUiConfigApi *get() {
  return &kConfigApi;
}

} // namespace ConfigApi
} // namespace framework
