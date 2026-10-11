// rmlui::steamapi implementation - backend selection + the shared facade
// (see SteamAPI.h for the layering rationale).
#include "SteamAPI.h"

#include "SteamApiBackend.h"

namespace steamapi {

namespace {

SteamApiBackend* Active = nullptr;

}

void init() {
	if (Active)
		return;
	// Bridge first (the D-07 direction); its connect() only succeeds while
	// it carries the full consumer coverage, so the native backend serves
	// today and the bridge takes over when upstream catches up.
	for (SteamApiBackend* candidate : {bridgeBackend(), nativeBackend()}) {
		if (candidate->connect()) {
			Active = candidate;
			return;
		}
	}
}

void shutdown() {
	Active = nullptr;
}

Backend backend() {
	return Active ? Active->kind() : Backend::None;
}

bool available() {
	return Active != nullptr;
}

SteamId64 localSteamId() {
	return Active ? Active->localSteamId() : 0;
}

std::string personaName(SteamId64 id) {
	return Active ? Active->personaName(id) : std::string();
}

bool avatar(SteamId64 id, AvatarRgba& out) {
	return Active ? Active->avatar(id, out) : false;
}

}
