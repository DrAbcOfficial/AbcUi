#pragma once

// Process-owned engine command bridges, installed on explicit module request.
// Module subscriptions and callback ownership live in EventBus. Bridges remain
// inert pass-throughs after their handlers are revoked, so other plugins may
// safely retain a bridge thunk as their predecessor.

#include <abcui/module_api.h>

namespace commands {

// Main-thread, module-owned subscription to any existing callback-backed
// engine command. No name whitelist, fixed slot count or init-only restriction.
// Returns zero on refusal. Revoke through events->unsubscribe.
AbcUiSubscription subscribe(const char *name, int priority, AbcUiEventVeto sink,
                            void *userdata);

} // namespace commands

namespace commands {
// Main-thread interaction state; does not depend on a concrete panel header.
bool textMenuOpen();
void setTextMenuOpen(bool open);
} // namespace commands
