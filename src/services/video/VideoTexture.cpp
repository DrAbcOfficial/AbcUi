// services::video::VideoTexture implementation - see VideoTexture.h.
#include "VideoTexture.h"

#include <abc/Console.h>

#include "../../rmlui/AbcRenderInterface.h"
#include "../../rmlui/RmlUiModule.h"
#include "../../rmlui/VGUI2ExtensionImport.h"

namespace video {

namespace {
// Process-unique registration prefix per texture instance, so two elements can
// never share one external registration.
std::string makePrefix() {
	static unsigned long long Counter = 0;
	return "videotex://" + std::to_string(++Counter) + "/";
}
} // namespace

VideoTexture::VideoTexture() : Prefix(makePrefix()), Uri(Prefix + "0") {}

VideoTexture::~VideoTexture() {
	release();
}

void VideoTexture::release() {
	if (auto* interface = rmlui::renderInterface())
		interface->unregisterExternalTexture(Prefix);
	if (TextureId != 0) {
		vgui2ext::deleteTextureById(TextureId);
		TextureId = 0;
	}
	Width = Height = 0;
	Uri = Prefix + "0";
}

bool VideoTexture::upload(const unsigned char* rgba, int width, int height) {
	if (!rgba || width <= 0 || height <= 0)
		return false;
	if (TextureId == 0 || width != Width || height != Height) {
		if (auto* interface = rmlui::renderInterface())
			interface->unregisterExternalTexture(Prefix);
		if (TextureId != 0) {
			vgui2ext::deleteTextureById(TextureId);
			TextureId = 0;
		}
		const int id = vgui2ext::createTexture();
		if (id == 0) {
			static bool warned = false;
			if (!warned) {
				abc::ConsoleWarn("[AbcUi video] surface texture channel unavailable; video disabled\n");
				warned = true;
			}
			return false;
		}
		TextureId = id;
		Width = width;
		Height = height;
		++Generation;
		Uri = Prefix + std::to_string(Generation);
	}
	vgui2ext::uploadTexture(TextureId, rgba, width, height);
	if (auto* interface = rmlui::renderInterface())
		interface->registerExternalTexture(Prefix, static_cast<Rml::TextureHandle>(TextureId), Width, Height, nullptr);
	return true;
}

} // namespace video
