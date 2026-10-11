// rmlui::steamapi native backend - direct connection to the game's
// steam_api.dll (user directive: the bridge lacks Friends/avatar coverage).
// from: ABCEnchance src/core/resource/playerresource.cpp + vgui_controls/
// avatar_image.cpp semantics, minus the import-library link: every export is
// bound with GetProcAddress so the plugin still loads (degraded) on setups
// without a live Steam client.
//
// Interface pointers come from the dll's SteamUser()/SteamFriends()/
// SteamUtils() accessors; the game owns SteamAPI_Init/RunCallbacks, so this
// backend only reads. Avatar download is the old poll-with-throttle shape:
// GetSmallFriendAvatar returns 0 until Steam fetched the image, callers
// retry through steamapi::avatar().

#include "SteamApiBackend.h"

#include <Windows.h>

#include <steam/isteamuser.h>
#include <steam/isteamfriends.h>
#include <steam/isteamutils.h>

#include <abc/Console.h>

#include <unordered_map>
#include <vector>

namespace steamapi {

namespace {

using FnAccessor = void* (*)();

struct BoundExports {
	FnAccessor user = nullptr;
	FnAccessor friends = nullptr;
	FnAccessor utils = nullptr;
};

BoundExports Exports;
ISteamUser* User = nullptr;
ISteamFriends* Friends = nullptr;
ISteamUtils* Utils = nullptr;

struct AvatarCacheEntry {
	int handle = 0; // steam image handle the buffer was decoded from
	std::vector<unsigned char> rgba;
	int width = 0;
	int height = 0;
};
std::unordered_map<SteamId64, AvatarCacheEntry> AvatarCache;

bool connectInterfaces() {
	if (!Exports.user || !Exports.friends || !Exports.utils)
		return false;
	User = static_cast<ISteamUser*>(Exports.user());
	Friends = static_cast<ISteamFriends*>(Exports.friends());
	Utils = static_cast<ISteamUtils*>(Exports.utils());
	// SteamAPI_Init has not run (or failed) when the accessors yield null;
	// treat as unavailable instead of crashing on the first vtable call.
	if (!User || !Friends || !Utils)
		return false;
	return true;
}

}

SteamApiBackend* nativeBackend() {
	static struct Backend final : SteamApiBackend {
		bool connect() override {
			const HMODULE module = GetModuleHandleA("steam_api.dll");
			if (!module)
				return false;
			Exports.user = reinterpret_cast<FnAccessor>(GetProcAddress(module, "SteamUser"));
			Exports.friends = reinterpret_cast<FnAccessor>(GetProcAddress(module, "SteamFriends"));
			Exports.utils = reinterpret_cast<FnAccessor>(GetProcAddress(module, "SteamUtils"));
			return connectInterfaces();
		}

		steamapi::Backend kind() override { return steamapi::Backend::NativeApi; }

		SteamId64 localSteamId() override {
			if (!User)
				return 0;
			return User->GetSteamID().ConvertToUint64();
		}

		std::string personaName(SteamId64 id) override {
			if (!Friends || !id)
				return {};
			const char* name = Friends->GetFriendPersonaName(CSteamID(id));
			return name ? std::string(name) : std::string();
		}

		bool avatar(SteamId64 id, AvatarRgba& out) override {
			if (!Friends || !Utils || !id)
				return false;
			int handle = Friends->GetSmallFriendAvatar(CSteamID(id));
			if (!handle) {
				// Not fetched yet: nudge the download (old CAvatarImage
				// RequestUserInformation - non-friends need it for the
				// persona/avatar to arrive at all) and let the caller retry.
				Friends->RequestUserInformation(CSteamID(id), false);
				return false; // steam still downloading (old k_uAvatarImageNotLoaded path)
			}

			AvatarCacheEntry& entry = AvatarCache[id];
			if (entry.handle == handle && !entry.rgba.empty()) {
				out.rgba = entry.rgba.data();
				out.width = entry.width;
				out.height = entry.height;
				return true;
			}

			uint32_t width = 0, height = 0;
			if (!Utils->GetImageSize(handle, &width, &height) || !width || !height)
				return false;
			entry.rgba.resize(static_cast<size_t>(width) * height * 4);
			if (!Utils->GetImageRGBA(handle, entry.rgba.data(),
				static_cast<int>(entry.rgba.size())))
				return false;
			entry.handle = handle;
			entry.width = static_cast<int>(width);
			entry.height = static_cast<int>(height);
			out.rgba = entry.rgba.data();
			out.width = entry.width;
			out.height = entry.height;
			return true;
		}
	} instance;
	return &instance;
}

}
