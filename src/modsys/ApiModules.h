#pragma once

// framework::ModulesApi - the sdk modules section (the module manager page's
// data and cache seam). See ApiTable.h for how it joins the module table.

#include <abcui/module_api.h>

namespace framework {
namespace ModulesApi {

const AbcUiModulesApi *get();

} // namespace ModulesApi
} // namespace framework
