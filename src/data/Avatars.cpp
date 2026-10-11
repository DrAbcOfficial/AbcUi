// data::avatars implementation - see Avatars.h.
#include <metahook.h>

// Windows.h min macro fights std::min (old repo precedent).
#undef min
#undef max

#include "Avatars.h"

#include <algorithm>
#include <unordered_map>

#include <abc/Paths.h>

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
	// Mod-relative TGA through FreeImage? AbcRenderInterface already owns a
	// FreeImage file path - but this layer must not depend on Rml, so decode
	// via FreeImage directly (same as the render interface's loadFileTexture
	// helper; keep the two in sync on flip semantics).
	const std::string path = abc::ModPath("abcui/img/default_avatar.tga");
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return false;
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	std::vector<unsigned char> bytes(static_cast<size_t>(size));
	const size_t read = fread(bytes.data(), 1, bytes.size(), file);
	fclose(file);
	if (read != bytes.size())
		return false;

	// Minimal uncompressed/RLE TGA32 decode (the pain_indicator tooling
	// proved the layout; header fields validated before use).
	if (bytes.size() < 18)
		return false;
	const int idLength = bytes[0];
	const int imageType = bytes[2];
	if (imageType != 2 && imageType != 10)
		return false;
	const int width = bytes[12] | (bytes[13] << 8);
	const int height = bytes[14] | (bytes[15] << 8);
	const int bpp = bytes[16];
	const bool topDown = (bytes[17] & 0x20) != 0;
	if ((bpp != 32 && bpp != 24) || width <= 0 || height <= 0)
		return false;
	const int bytesPerPixel = bpp / 8;

	std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
	if (bytes[1] != 0 || (bytes[17] & 0x10) != 0)
		return false;
	size_t src = 18 + idLength;
	if (src > bytes.size())
		return false;
	if (imageType == 2) {
		if (bytes.size() - src < static_cast<size_t>(width) * height * bytesPerPixel)
			return false;
		// Uncompressed rows: BGRA/BGR -> RGBA with full alpha for 24bpp.
		for (int i = 0; i < width * height; i++) {
			const unsigned char* in = bytes.data() + src + static_cast<size_t>(i) * bytesPerPixel;
			rgba[i * 4 + 0] = in[2];
			rgba[i * 4 + 1] = in[1];
			rgba[i * 4 + 2] = in[0];
			rgba[i * 4 + 3] = bpp == 32 ? in[3] : 255;
		}
	} else {
		size_t dst = 0;
		while (dst < rgba.size() && src < bytes.size()) {
			const unsigned char header = bytes[src++];
			const int count = (header & 0x7f) + 1;
			if (static_cast<size_t>(count) > (rgba.size() - dst) / 4)
				return false;
			const size_t packetBytes = static_cast<size_t>(count) * bytesPerPixel;
			if (header & 0x80) {
				if (src + bytesPerPixel > bytes.size())
					return false;
				for (int i = 0; i < count && dst < rgba.size(); i++) {
					rgba[dst + 0] = bytes[src + 2];
					rgba[dst + 1] = bytes[src + 1];
					rgba[dst + 2] = bytes[src + 0];
					rgba[dst + 3] = bpp == 32 ? bytes[src + 3] : 255;
					dst += 4;
				}
				src += bytesPerPixel;
			} else {
				if (src + packetBytes > bytes.size())
					return false;
				for (int i = 0; i < count && dst < rgba.size(); i++) {
					const unsigned char* in = bytes.data() + src + static_cast<size_t>(i) * bytesPerPixel;
					rgba[dst + 0] = in[2];
					rgba[dst + 1] = in[1];
					rgba[dst + 2] = in[0];
					rgba[dst + 3] = bpp == 32 ? in[3] : 255;
					dst += 4;
				}
				src += packetBytes;
			}
		}
		if (dst != rgba.size())
			return false;
	}
	if (!topDown) {
		// Stored bottom-up: flip to Rml's top-down convention.
		std::vector<unsigned char> flipped(rgba.size());
		const size_t stride = static_cast<size_t>(width) * 4;
		for (int y = 0; y < height; y++)
			memcpy(flipped.data() + (height - 1 - y) * stride, rgba.data() + y * stride, stride);
		rgba = std::move(flipped);
	}
	if (!vgui2ext::available())
		return false;
	DefaultAvatarTexture = vgui2ext::createTexture();
	if (!DefaultAvatarTexture)
		return false;
	vgui2ext::uploadTexture(DefaultAvatarTexture, rgba.data(), width, height);
	DefaultAvatarWidth = width;
	DefaultAvatarHeight = height;
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
