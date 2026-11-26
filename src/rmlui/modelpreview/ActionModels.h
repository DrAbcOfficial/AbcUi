#pragma once

#include <functional>
#include <string>

namespace modelpreview {

// The path the file APIs use for an engine-relative path (abc::ModPath in
// the framework).
using NativePath = std::function<std::string(const std::string &)>;

// Materialize the packaged stance models for the engine's file-only Studio
// loader: every model directly under `directory` (a mod:// directory) is
// written to actionModelDir() + name, through `path`. A file already there
// is never rewritten - the user's, or one an earlier run wrote - and through
// the custom overlay it replaces the packaged model, so a packaged model
// that a module upgrade changed stays shadowed until that file is deleted.
// Failure is reported without preventing the menu from loading.
void prepareActionModels(const char *directory, const NativePath &path);

} // namespace modelpreview
