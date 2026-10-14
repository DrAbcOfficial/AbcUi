#pragma once

// services::video::VideoTexture - a live RGBA texture fed by decoded frames
// and published to Rml documents as an external texture. The pattern is the
// batch-D radar one (registerExternalTexture + a stable URI whose query
// suffix only bumps when the GPU texture is recreated), so an element that
// keeps the same `src` string costs RmlUi zero texture work per frame while
// the pixels keep changing underneath.

#include <string>

namespace video {

class VideoTexture {
public:
	VideoTexture();
	~VideoTexture();
	VideoTexture(const VideoTexture&) = delete;
	VideoTexture& operator=(const VideoTexture&) = delete;

	// Uploads one frame (top-down tightly packed RGBA8). (Re)creates the GPU
	// texture when the size changed. Returns false when the VGUI surface
	// texture channel is unavailable.
	bool upload(const unsigned char* rgba, int width, int height);

	// Rml image source for the current texture; empty before the first upload.
	const std::string& uri() const { return Uri; }
	bool valid() const { return TextureId != 0; }
	// Raw texture id for consumers that draw the frame themselves (the menu
	// backdrop blurs it instead of showing it directly).
	int textureId() const { return TextureId; }

	void release();

private:
	int TextureId = 0;
	int Width = 0;
	int Height = 0;
	unsigned int Generation = 0;
	// Registration key handed to AbcRenderInterface (stable for the instance)
	// and the image source the element puts on its <img> (bumped whenever the
	// GPU texture is recreated, so Rml re-resolves exactly once).
	std::string Prefix;
	std::string Uri;
};

} // namespace video
