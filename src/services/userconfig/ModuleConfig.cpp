#include "ModuleConfig.h"

namespace services::userconfig {
namespace {

bool sectionChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

} // namespace

bool validModuleSection(std::string_view local) {
  if (local.empty() || local.size() > kMaxModuleSectionName)
    return false;
  for (const char c : local)
    if (!sectionChar(c))
      return false;
  return true;
}

std::string moduleSectionKey(std::string_view moduleId,
                             std::string_view local) {
  std::string key = "module.";
  key.append(moduleId);
  key.push_back('.');
  key.append(local);
  return key;
}

} // namespace services::userconfig
