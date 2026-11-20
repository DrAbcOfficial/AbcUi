#pragma once

// services::loading::LevelLoadGate - the engine loading-plaque fact used by
// world-dependent consumers (the player hull traces).
//
// The GameUI lifecycle reports "level" loads (CL_ParseServerData, only when
// cl.maxclients > 1) and "transition" loads (SCR_BeginLoadingPlaque, every
// changelevel). Both bracket a world replacement: Host_Changelevel_f starts
// the plaque before SV_ServerShutdown/SV_SpawnServer, and SV_SpawnServer's
// Host_ClearMemory frees the old world before any client disconnect/reconnect
// is processed.
//
// This fact is deliberately separate from NativeUiBridge::hasLevel(), which
// is the connected-only menu fact: a listen-server changelevel keeps the
// session connected, so HasLevel stays true while the world is already gone
// (issue #139). Only world-dependent reads need this gate.

#include <cstring>

namespace services::loading {

// Engine resource types that describe a level/world transition. "level" is
// the map/connect stream; "transition" is SCR_BeginLoadingPlaque, the only
// start event on a listen server whose cl.maxclients is 1.
inline bool isLevelLoadResource(const char *resourceType) {
  return resourceType && (!_stricmp(resourceType, "level") ||
                          !_stricmp(resourceType, "transition"));
}

// True from LoadingStarted("level"|"transition") until a corresponding
// LoadingFinished or a server disconnect. A missed finish is fail-safe: the
// gate stays closed and the traces keep their last cached value instead of
// walking a replaced world.
class LevelLoadGate {
public:
  void started() {
    Loading = true;
  }
  void finished() {
    Loading = false;
  }
  void disconnected() {
    Loading = false;
  }
  bool active() const {
    return Loading;
  }

private:
  bool Loading = false;
};

} // namespace services::loading
