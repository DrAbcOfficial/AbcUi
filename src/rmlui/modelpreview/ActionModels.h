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

// Make the engine's copy of the action models match actionModelDir():
// every model directly in the directory is written to
// kActionModelLoadDir + name when it is missing or differs there, and a
// copy without its model is removed. The engine can only load a model whose
// game-relative name fits MAX_MODEL_NAME, which the directory itself is too
// long for, so it reads this mirror and never the directory. A stem that
// fits not even here is reported once and gets no copy. Failure is reported
// without preventing the menu from loading.
void mirrorActionModels(const NativePath &path);

} // namespace modelpreview
