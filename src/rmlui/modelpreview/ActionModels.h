#pragma once

namespace modelpreview {

// Materialize packaged stance models for the engine's file-only Studio loader.
// Failure is reported without preventing the menu from loading.
void prepareActionModels(const char *directory);

} // namespace modelpreview
