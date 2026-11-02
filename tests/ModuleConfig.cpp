// abc_ui_module_config - the module-scoped config key rules shared by the
// config ABI: the local-name grammar, the "module.<id>.<local>" composition
// and the section bound. Pure string logic, no engine and no file access.
#include "framework/TestHarness.h"
#include "services/userconfig/ModuleConfig.h"

#include <cstddef>
#include <string>

using namespace services::userconfig;

int main() {
  TEST_CASE("local-name");
  CHECK(validModuleSection("weapon_filter"));
  CHECK(validModuleSection("a"));
  CHECK(validModuleSection("a1_"));
  CHECK(validModuleSection(std::string(kMaxModuleSectionName, 'a')));

  TEST_CASE("local-name-rejects");
  CHECK(!validModuleSection(""));
  CHECK(!validModuleSection(std::string(kMaxModuleSectionName + 1, 'a')));
  CHECK(!validModuleSection("Weapon_Filter"));
  CHECK(!validModuleSection("weapon.filter"));
  CHECK(!validModuleSection("weapon/filter"));
  CHECK(!validModuleSection("weapon\\filter"));
  CHECK(!validModuleSection("weapon filter"));
  CHECK(!validModuleSection("weapon-filter"));
  CHECK(!validModuleSection("module.abcui.weaponfilter"));
  CHECK(!validModuleSection("weapon_filter\n"));

  TEST_CASE("key");
  CHECK(moduleSectionKey("abcui.weaponfilter", "weapon_filter") ==
        "module.abcui.weaponfilter.weapon_filter");
  CHECK(moduleSectionKey("abcui.weaponfilter", "a") ==
        "module.abcui.weaponfilter.a");
  // The local name never carries a dot, so the id is the only dotted part.
  CHECK(moduleSectionKey("abcui.gameui", "x") == "module.abcui.gameui.x");

  TEST_CASE("bounds");
  CHECK(kMaxModuleSectionBytes == 64 * 1024);
  CHECK(kMaxModuleSectionName == 32);

  return abctest::finish("abc_ui_module_config");
}
