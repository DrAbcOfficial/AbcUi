#pragma once

// services::userconfig - module-scoped config.json sections.
//
// A module reads and writes config.json only through its own key namespace:
//
//   module.<module-id>.<local-name>
//
// The local name is [a-z0-9_]{1,32} and the module id is the manifest id, so
// a module can never reach another module's section or a top-level field such
// as render_backend. The helpers here are pure (no engine access) and shared
// by the config ABI and its offline tests.

#include <cstddef>
#include <string>
#include <string_view>

namespace services::userconfig {

// Longest local section name validModuleSection accepts.
inline constexpr std::size_t kMaxModuleSectionName = 32;

// Largest section the module config API accepts, JSON bytes plus terminator.
inline constexpr std::size_t kMaxModuleSectionBytes = 64 * 1024;

// True for [a-z0-9_]{1,32}: no dots, path separators, spaces or uppercase, so
// a local name can never escape the module's namespace.
bool validModuleSection(std::string_view local);

// The full config.json key of a module section. Callers validate the local
// name first; the module id comes from the manifest.
std::string moduleSectionKey(std::string_view moduleId, std::string_view local);

} // namespace services::userconfig
