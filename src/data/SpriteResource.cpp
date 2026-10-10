// data::sprites implementation - see SpriteResource.h.
// from: ABCEnchance/src/core/resource/spriteresource.* (hud.txt half);
// .spr pixel decoding per SPRView.Net Core/SprReader.cs + FrameDecoder.cs.
#include <metahook.h>

#include "SpriteResource.h"

#include <wrect.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include <abc/Console.h>
#include <abc/Paths.h>
#include <abc/Render.h>

#include "../rmlui/VGUI2ExtensionImport.h"

namespace sprites {

namespace {

constexpr int kSprSignature = 0x50534449; // "IDSP"
constexpr int kSprVersion = 2;

enum SpriteFormat {
	kFormatNormal = 0,
	kFormatAdditive = 1,
	kFormatIndexAlpha = 2,
	kFormatAlphaTest = 3,
};

struct HudEntry {
	std::string atlasFile;
	wrect_t rect;
};

struct DecodedAtlas {
	int width = 0;
	int height = 0;
	std::vector<unsigned char> rgba; // top-down RGBA8888
};

std::unordered_map<std::string, HudEntry> HudEntries;
std::unordered_map<std::string, DecodedAtlas> Atlases;
std::unordered_map<std::string, int> CropTextures;
bool ChannelWarned = false;

// GoldSrc keeps sprites in several search paths; the engine SPR_Load walks
// the same list (downloads first for server-shipped custom sprites).
std::vector<std::string> spriteSearchPaths(const std::string& relative) {
	std::string gamedir = abc::GameDir();
	return {
		abc::ModPath(relative.c_str()),
		abc::ModPath((gamedir + "_downloads/" + relative).c_str()),
		abc::ModPath((gamedir + "_addon/" + relative).c_str()),
	};
}

bool readWholeFile(const std::string& path, std::vector<unsigned char>& out) {
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "rb") != 0 || !file)
		return false;
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	out.resize(static_cast<size_t>(size));
	const size_t read = fread(out.data(), 1, out.size(), file);
	fclose(file);
	return read == out.size();
}

void decodeFramePixels(const unsigned char* indexed, int pixelCount, const unsigned char* palette,
	int paletteSize, int format, unsigned char* rgba) {
	const bool fullPalette = paletteSize == 256;
	const bool indexIsAlpha = format == kFormatIndexAlpha;
	const bool blackIsEmpty = format == kFormatAdditive;
	// Key colour = palette entry 255 (colour compare, not index compare) for
	// every non-IndexAlpha format with a full palette (SPRView rule).
	unsigned char keyR = 0, keyG = 0, keyB = 0;
	if (!indexIsAlpha && fullPalette) {
		keyR = palette[255 * 3 + 0];
		keyG = palette[255 * 3 + 1];
		keyB = palette[255 * 3 + 2];
	}
	for (int i = 0; i < pixelCount; i++) {
		const int index = indexed[i];
		const int entry = (index < paletteSize ? index : paletteSize - 1) * 3;
		unsigned char r = palette[entry + 0];
		unsigned char g = palette[entry + 1];
		unsigned char b = palette[entry + 2];
		unsigned char a = 255;
		if (indexIsAlpha) {
			a = static_cast<unsigned char>(index);
		}
		else if ((blackIsEmpty && r == 0 && g == 0 && b == 0) ||
				 (fullPalette && r == keyR && g == keyG && b == keyB)) {
			a = 0;
		}
		unsigned char* px = rgba + i * 4;
		px[0] = r;
		px[1] = g;
		px[2] = b;
		px[3] = a;
	}
}

bool loadSpr(const std::string& atlasFile, DecodedAtlas& out) {
	std::vector<unsigned char> file;
	for (const auto& path : spriteSearchPaths("sprites/" + atlasFile)) {
		if (readWholeFile(path, file))
			break;
	}
	if (file.empty()) {
		abc::ConsoleWarn("[AbcUi] sprite atlas not found: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	const auto readInt = [&file](size_t offset, int& value) {
		if (offset + 4 > file.size())
			return false;
		value = static_cast<int>(file[offset]) | (static_cast<int>(file[offset + 1]) << 8) |
			(static_cast<int>(file[offset + 2]) << 16) | (static_cast<int>(file[offset + 3]) << 24);
		return true;
	};

	int signature = 0, version = 0, format = 0;
	if (!readInt(0, signature) || signature != kSprSignature || !readInt(4, version) ||
		version != kSprVersion || !readInt(12, format)) {
		abc::ConsoleWarn("[AbcUi] unsupported sprite format: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	int frameCount = 0;
	if (!readInt(28, frameCount) || frameCount <= 0) {
		abc::ConsoleWarn("[AbcUi] sprite declares no frames: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	size_t offset = 40;
	int paletteSize = 0;
	if (offset + 2 > file.size())
		return false;
	paletteSize = static_cast<int>(file[offset]) | (static_cast<int>(file[offset + 1]) << 8);
	offset += 2;
	if (paletteSize <= 0 || offset + static_cast<size_t>(paletteSize) * 3 > file.size()) {
		abc::ConsoleWarn("[AbcUi] sprite palette corrupt: sprites/%s\n", atlasFile.c_str());
		return false;
	}
	const unsigned char* palette = file.data() + offset;
	offset += static_cast<size_t>(paletteSize) * 3;

	// First readable frame; malformed tails end the table (SPRView rule -
	// e.g. SC's misc/wei.spr declares 22 frames, the last one is junk).
	int frameW = 0, frameH = 0;
	const unsigned char* pixels = nullptr;
	for (int i = 0; i < frameCount && pixels == nullptr; i++) {
		int fields[5];
		bool truncated = false;
		for (int f = 0; f < 5; f++) {
			if (!readInt(offset + f * 4, fields[f])) {
				truncated = true;
				break;
			}
		}
		if (truncated)
			break;
		offset += 20;
		frameW = fields[3];
		frameH = fields[4];
		const long pixelCount = static_cast<long>(frameW) * frameH;
		if (frameW <= 0 || frameH <= 0 || offset + pixelCount > file.size())
			break;
		pixels = file.data() + offset;
		offset += pixelCount;
	}
	if (!pixels) {
		abc::ConsoleWarn("[AbcUi] sprite has no readable frame: sprites/%s\n", atlasFile.c_str());
		return false;
	}

	out.width = frameW;
	out.height = frameH;
	out.rgba.resize(static_cast<size_t>(frameW) * frameH * 4);
	decodeFramePixels(pixels, frameW * frameH, palette, paletteSize, format, out.rgba.data());
	return true;
}

int createTexture(const DecodedAtlas& atlas) {
	if (!vgui2ext::available()) {
		if (!ChannelWarned) {
			ChannelWarned = true;
			abc::ConsoleWarn("[AbcUi] sprite textures unavailable: VGUI2Extension missing\n");
		}
		return 0;
	}
	const int id = vgui2ext::createTexture();
	if (!id)
		return 0;
	vgui2ext::uploadTexture(id, atlas.rgba.data(), atlas.width, atlas.height);
	return id;
}

DecodedAtlas* ensureAtlas(const std::string& atlasFile) {
	const auto key = atlasFile;
	auto itor = Atlases.find(key);
	if (itor != Atlases.end())
		return &itor->second;
	DecodedAtlas atlas;
	if (!loadSpr(key, atlas))
		return nullptr;
	return &Atlases.emplace(key, std::move(atlas)).first->second;
}

} // namespace

void vidInit() {
	if (!HudEntries.empty())
		return;
	// Same engine-side parse the old gSpriteRes used; entries below the 640
	// res band are skipped exactly like the old filter (screen < 640 wide
	// selects the 320 set - irrelevant for the 1080p+ HUD, keep 640 only
	// when the screen is small).
	int count = 0;
	client_sprite_t* list = gEngfuncs.pfnSPR_GetList(const_cast<char*>("sprites/hud.txt"), &count);
	if (!list)
		return;
	// Old filter (width-based, exactly as the old gSpriteRes used).
	const int screenW = abc::MetaRenderer() ? static_cast<int>(abc::MetaRenderer()->GetSwapChainWidth()) : 1920;
	const int wantedRes = screenW < 640 ? 320 : 640;
	for (int i = 0; i < count; i++) {
		if (list[i].iRes != wantedRes)
			continue;
		HudEntry entry;
		entry.atlasFile = std::string(list[i].szSprite) + ".spr";
		entry.rect = list[i].rc;
		HudEntries[list[i].szName] = std::move(entry);
	}
}

void shutdown() {
	for (const auto& [key, texture] : CropTextures) {
		if (texture)
			vgui2ext::deleteTextureById(texture);
	}
	CropTextures.clear();
	Atlases.clear();
	HudEntries.clear();
}

bool findHudSprite(const char* name, std::string& atlasFile, wrect_t& rect) {
	const auto itor = HudEntries.find(name);
	if (itor == HudEntries.end())
		return false;
	atlasFile = itor->second.atlasFile;
	rect = itor->second.rect;
	return true;
}

int croppedTexture(const std::string& atlasFile, const wrect_t& rect) {
	const int left = rect.left, top = rect.top;
	const int width = rect.right - rect.left;
	const int height = rect.bottom - rect.top;
	if (width <= 0 || height <= 0)
		return 0;

	const std::string key = atlasFile + "|" + std::to_string(left) + "," + std::to_string(top) +
		"," + std::to_string(width) + "," + std::to_string(height);
	const auto itor = CropTextures.find(key);
	if (itor != CropTextures.end())
		return itor->second;

	DecodedAtlas* atlas = ensureAtlas(atlasFile);
	if (!atlas)
		return 0;

	DecodedAtlas crop;
	crop.width = width;
	crop.height = height;
	crop.rgba.resize(static_cast<size_t>(width) * height * 4);
	for (int y = 0; y < height; y++) {
		const int srcY = top + y;
		if (srcY < 0 || srcY >= atlas->height)
			continue;
		const int copyW = width < atlas->width - left ? width : atlas->width - left;
		if (copyW <= 0)
			continue;
		memcpy(crop.rgba.data() + static_cast<size_t>(y) * width * 4,
			atlas->rgba.data() + (static_cast<size_t>(srcY) * atlas->width + left) * 4,
			static_cast<size_t>(copyW) * 4);
	}
	const int id = createTexture(crop);
	CropTextures[key] = id;
	return id;
}

int textureForUri(const std::string& uri, int& width, int& height) {
	if (uri.rfind("spr://", 0) == 0) {
		std::string atlasFile;
		wrect_t rect{};
		if (!findHudSprite(uri.c_str() + 6, atlasFile, rect))
			return 0;
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;
		return croppedTexture(atlasFile, rect);
	}
	if (uri.rfind("spricon://", 0) == 0) {
		// spricon://<atlas>/<l>,<t>,<r>,<b>
		const size_t slash = uri.find('/', 10);
		if (slash == std::string::npos)
			return 0;
		const std::string atlasFile = uri.substr(10, slash - 10);
		wrect_t rect{};
		if (sscanf_s(uri.c_str() + slash + 1, "%d,%d,%d,%d", &rect.left, &rect.top, &rect.right, &rect.bottom) != 4)
			return 0;
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;
		return croppedTexture(atlasFile, rect);
	}
	return 0;
}

}
