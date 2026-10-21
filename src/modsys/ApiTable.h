#pragma once

// framework::ApiTable - the C ABI service table handed to module entries.
//
// The table and its section structs live in sdk/abcui/module_api.h; this
// header is the framework-side owner of the single frozen instance.

#include "module_api.h"

namespace framework {

namespace ApiTable {

// The one table instance; its layout is append-only (see abi.h).
const AbcUiModuleApi *get();

} // namespace ApiTable

} // namespace framework
