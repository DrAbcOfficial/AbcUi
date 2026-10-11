#pragma once

// framework::ImageLoader - FreeImage-backed multi-format image decoding
// (PNG/JPG/TGA/BMP/... and everything else the pinned FreeImage_clone reads).
// Feeds every AbcUi texture source that starts as an encoded image file or
// buffer (plan 05 §3.2 LoadTexture path).
//
// Seam rules: decode only - no Rml types, no VGUI2 surface, no renderer
// calls. Consumers upload DecodedImage through their own texture channel
// (rmlui::AbcRenderInterface for RML-referenced files, data::avatars for the
// default avatar). The single FreeImage dependency of the plugin lives here;
// callers never include FreeImage.h.

#include <cstddef>
#include <vector>

namespace framework {

namespace ImageLoader {

// Decoded pixels: top-down RGBA8, row-major, Width * Height * 4 bytes.
// FreeImage's bottom-up BGRA scan order is normalized away here so every
// consumer sees the same layout (matching Rml's (0,0)=top-left UV convention).
struct DecodedImage {
	std::vector<unsigned char> Pixels;
	int Width = 0;
	int Height = 0;

	bool valid() const { return Width > 0 && Height > 0 && !Pixels.empty(); }
};

// Detects the format from the file content (file signature, with the name
// only as fallback for headerless formats) and decodes it. Returns false
// silently on any failure (unknown format, unreadable, bad header) - the
// asset verification layer owns missing-file warnings, LoadTexture retries
// are Rml's business.
bool loadFromFile(const char* path, DecodedImage& out);

// Same contract for an in-memory buffer (data already pulled through another
// channel). Format detection runs on the leading bytes.
bool loadFromMemory(const unsigned char* data, std::size_t size, DecodedImage& out);

}

}
