#pragma once

// data::avatars - scoreboard avatar textures (steamapi facade consumer).
// "avatar://<steamid64>" URIs resolve here from AbcRenderInterface::LoadTexture:
// not-yet-downloaded avatars get the bundled default and a background fetch
// kicks off; ready ones upload through the VGUI surface texture channel.
// The scoreboard refresh rewrites ready rows' src (cache-bust suffix) so Rml
// re-resolves once the RGBA landed.

#include <cstdint>

namespace avatars {

void init();
void shutdown();

// Texture id for a URI load (never 0 - falls back to the default avatar);
// width/height receive the pixel size. `triggerFetch` starts the steamapi
// download attempt when the id is not ready yet.
int textureFor(std::uint64_t steamId64, int& width, int& height, bool triggerFetch);

}
