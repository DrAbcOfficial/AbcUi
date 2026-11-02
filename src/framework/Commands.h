#pragma once

// Process-owned engine command bridges. A bridge exists only for a command a
// module claimed through the arbitration API; the bridge dispatches the
// command.<name> domain and calls the displaced predecessor only when no
// claim handled the occurrence. Bridges remain inert pass-throughs after
// their last claim is revoked, so another plugin may safely retain the thunk
// as its predecessor.

namespace commands {

// Installs the arbitration command provider and the process-scoped hook
// diagnostics command. Call once from HUD_Init before modules load.
void init();

// Retries bridges whose engine command did not exist yet (the client
// registers MediaPlayer after module load). Cheap while no bridge is pending.
void pump();

// Re-validates every bridge against the engine command table and re-hooks a
// bridge the client restored; a foreign thunk is reported as shadowed and is
// never displaced.
void refreshAfterClientLoad();

} // namespace commands
