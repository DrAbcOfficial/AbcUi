// data::avatars implementation - see Avatars.h.
#include <metahook.h>

#include "Avatars.h"

#include <unordered_map>

#include <abc/Paths.h>

#include "../framework/ImageLoader.h"
#include "../hooks.h"
#include "../rmlui/SteamAPI.h"
#include "../rmlui/VGUI2ExtensionImport.h"

namespace avatars {

namespace {

struct Entry {
	std::vector<unsigned char> rgba; // uploaded/ready pixels
	int width = 0;
	int height = 0;
	int textureId = 0; // 0 = not uploaded yet
	int generation = 0; // bumped on upload - the scoreboard cache-bust suffix
	double nextAttempt = 0.0; // steam retry throttle (old m_fNextLoadTime)
};

// Monotonic client clock for the throttle; frametime-independent.
double NowSeconds() {
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

std::unordered_map<std::uint64_t, Entry> Entries;
int DefaultAvatarTexture = 0;
int DefaultAvatarWidth = 0;
int DefaultAvatarHeight = 0;

bool loadDefaultAvatar() {
	// Mod-relative image through the shared FreeImage loader (format is
	// detected from the file content, so the shipped asset's format is free
	// to change without touching this layer).
	if (!vgui2ext::available())
		return false;
	framework::ImageLoader::DecodedImage image;
	if (!framework::ImageLoader::loadFromFile(abc::ModPath("abcui/img/default_avatar.png").c_str(), image))
		return false;
	DefaultAvatarTexture = vgui2ext::createTexture();
	if (!DefaultAvatarTexture)
		return false;
	vgui2ext::uploadTexture(DefaultAvatarTexture, image.Pixels.data(), image.Width, image.Height);
	DefaultAvatarWidth = image.Width;
	DefaultAvatarHeight = image.Height;
	return true;
}

bool upload(Entry& entry) {
	if (!vgui2ext::available())
		return false;
	if (!entry.textureId)
		entry.textureId = vgui2ext::createTexture();
	if (!entry.textureId)
		return false;
	vgui2ext::uploadTexture(entry.textureId, entry.rgba.data(), entry.width, entry.height);
	return true;
}

}

void init() {
	loadDefaultAvatar();
}

void shutdown() {
	// Texture ids are NOT deleted here: every id reaches Rml through
	// LoadTexture (avatar:// or the default fallback) and Rml::Shutdown
	// releases them via ReleaseTexture (the single delete point - same
	// lifecycle rule as the sprite module).
	Entries.clear();
	DefaultAvatarTexture = 0;
	DefaultAvatarWidth = 0;
	DefaultAvatarHeight = 0;
}

int textureFor(std::uint64_t steamId64, int& width, int& height, bool triggerFetch) {
	if (!steamId64 || !DefaultAvatarTexture) {
		width = DefaultAvatarWidth;
		height = DefaultAvatarHeight;
		return DefaultAvatarTexture;
	}

	Entry& entry = Entries[steamId64];
	if (entry.textureId && !entry.rgba.empty()) {
		width = entry.width;
		height = entry.height;
		return entry.textureId;
	}

	// Retryable with the old 1s throttle (CAvatarImage m_fNextLoadTime):
	// GetSmallFriendAvatar returns 0 while steam is still fetching, and the
	// throttle keeps the per-refresh polling from hammering the interface.
	const double now = NowSeconds();
	if (triggerFetch && entry.rgba.empty() && now >= entry.nextAttempt) {
		entry.nextAttempt = now + 1.0;
		steamapi::AvatarRgba avatar;
		if (steamapi::avatar(steamId64, avatar) && avatar.rgba) {
			entry.rgba.assign(avatar.rgba, avatar.rgba + static_cast<size_t>(avatar.width) * avatar.height * 4);
			entry.width = avatar.width;
			entry.height = avatar.height;
			if (upload(entry))
				entry.generation++;
			width = entry.width;
			height = entry.height;
			return entry.textureId;
		}
	}

	width = DefaultAvatarWidth;
	height = DefaultAvatarHeight;
	return DefaultAvatarTexture;
}

int generation(std::uint64_t steamId64) {
	const auto itor = Entries.find(steamId64);
	return itor == Entries.end() ? 0 : itor->second.generation;
}

}
