// rmlui::AbcRenderInterface implementation - see AbcRenderInterface.h for the
// semantics (blend conversion, texture ids, viewport-as-scissor R-1 verdict).
#include <metahook.h>

#include "AbcRenderInterface.h"

#include <cstring>

#include <abc/Console.h>
#include <abc/Paths.h>
#include <abc/Render.h>
#include <abc/RenderCommon.h>

#include "../data/Avatars.h"
#include "../data/SpriteResource.h"
#include "../framework/ImageLoader.h"
#include "VGUI2ExtensionImport.h"

namespace rmlui {

namespace {

// Upload row order of the VGUI2 surface DrawSetTextureRGBA channel, settled
// by the Phase 3 in-game spike: false = uploads land top-scanline-first at
// v=0, which matches RmlUi's (0,0)=top-left UV convention without extra
// flipping (the image itself is flipped out of FreeImage's bottom-up order
// in LoadTexture below).
constexpr bool kFlipTextureUpload = false;

constexpr uint64_t kProgramAlphaBlend = DRAW_TEXTURED_RECT_ALPHA_BLEND_ENABLED;
constexpr uint64_t kProgramFilledAlphaBlend = DRAW_FILLED_RECT_ALPHA_BLEND_ENABLED;
// Sprite icons: kRenderTransAdd parity (src*a + dest) - see header note.
constexpr uint64_t kProgramSpriteAdditive = DRAW_TEXTURED_RECT_ALPHA_BASED_ADDITIVE_ENABLED;

void ToStraightColor(const Rml::ColourbPremultiplied& in, float out[4]) {
	const float a = in.alpha / 255.0f;
	out[3] = a;
	if (a > 0.0f) {
		out[0] = (in.red / 255.0f) / a;
		out[1] = (in.green / 255.0f) / a;
		out[2] = (in.blue / 255.0f) / a;
	}
	else {
		out[0] = out[1] = out[2] = 0.0f;
	}
}

void UnpremultiplyInPlace(unsigned char* rgba, size_t pixelCount) {
	for (size_t i = 0; i < pixelCount; i++) {
		unsigned char* px = rgba + i * 4;
		const unsigned int a = px[3];
		if (a < 255) {
			px[0] = static_cast<unsigned char>(a > 0 ? (px[0] * 255u) / a : 0);
			px[1] = static_cast<unsigned char>(a > 0 ? (px[1] * 255u) / a : 0);
			px[2] = static_cast<unsigned char>(a > 0 ? (px[2] * 255u) / a : 0);
		}
	}
}

void FlipRows(unsigned char* pixels, int width, int height) {
	const int stride = width * 4;
	std::vector<unsigned char> row(stride);
	for (int y = 0; y < height / 2; y++) {
		unsigned char* a = pixels + static_cast<size_t>(y) * stride;
		unsigned char* b = pixels + static_cast<size_t>(height - 1 - y) * stride;
		memcpy(row.data(), a, stride);
		memcpy(a, b, stride);
		memcpy(b, row.data(), stride);
	}
}

int UploadNewTexture(const unsigned char* rgba, int width, int height) {
	const int id = vgui2ext::createTexture();
	if (id == 0) {
		static bool Warned = false;
		if (!Warned) {
			abc::ConsoleWarn("[AbcUi] texture upload unavailable (VGUI2Extension surface missing)\n");
			Warned = true;
		}
		return 0;
	}
	if constexpr (kFlipTextureUpload) {
		std::vector<unsigned char> flipped(rgba, rgba + static_cast<size_t>(width) * height * 4);
		FlipRows(flipped.data(), width, height);
		vgui2ext::uploadTexture(id, flipped.data(), width, height);
	}
	else {
		vgui2ext::uploadTexture(id, rgba, width, height);
	}
	return id;
}

} // namespace

AbcRenderInterface::~AbcRenderInterface() {
	endFrame();
}

void AbcRenderInterface::beginFrame() {
	// Frame brackets start with no 2D state installed; the first geometry
	// issues Set2D. (The framebuffer bind is done by RmlUiModule.)
}

void AbcRenderInterface::endFrame() {
	if (TwoDActive) {
		if (IMetaRenderer* renderer = abc::MetaRenderer())
			renderer->Finish2D();
		TwoDActive = false;
		AppliedScissor = false;
		AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
	}
}

Rml::CompiledGeometryHandle AbcRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
	if (vertices.empty() || indices.empty())
		return {};

	auto geometry = new CompiledGeometry();
	geometry->Vertices.assign(vertices.begin(), vertices.end());
	geometry->Indices.assign(indices.begin(), indices.end());
	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void AbcRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
	IMetaRenderer* renderer = abc::MetaRenderer();
	if (!renderer || geometry == 0)
		return;

	const auto* source = reinterpret_cast<const CompiledGeometry*>(geometry);
	const size_t vertexCount = source->Vertices.size();
	const size_t indexCount = source->Indices.size();
	if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0)
		return;

	// With scissor active Rml still sends geometry overlapping the region;
	// Set2DEx confines rasterization to the region rect.
	// An empty region (zero width/height - e.g. a fully shrunken overflow
	// clip) must draw NOTHING: falling through to the unclipped path would
	// leak the clipped content (the armor-bar-at-zero-width bug).
	if (ScissorEnabled) {
		if (!ScissorRect.Valid() || ScissorRect.Width() <= 0 || ScissorRect.Height() <= 0)
			return;
	}
	const bool scissor = ScissorEnabled;

	// Per-state 2D setup (matrix push/pop balanced by endFrame).
	if (!TwoDActive || AppliedScissor != scissor || (scissor && !(AppliedScissorRect == ScissorRect))) {
		endFrame();
		if (scissor) {
			// Set2DEx takes GL window coords (bottom-left origin); Rml rects
			// are top-left origin, hence the y flip around screen height.
			const int screenH = static_cast<int>(renderer->GetSwapChainHeight());
			renderer->Set2DEx(ScissorRect.Left(), screenH - ScissorRect.Bottom(), ScissorRect.Width(), ScissorRect.Height());
		}
		else {
			renderer->Set2D();
		}
		TwoDActive = true;
		AppliedScissor = scissor;
		AppliedScissorRect = ScissorRect;
	}

	if (texture == 0) {
		// Untextured geometry: the filled-rect fast path (05 §3.2).
		std::vector<filledrectvertex_t> vertices(vertexCount);
		for (size_t i = 0; i < vertexCount; i++) {
			const Rml::Vector2f pos = applyTransform(
				source->Vertices[i].position + translation);
			vertices[i].pos[0] = pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
			vertices[i].pos[1] = pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
			ToStraightColor(source->Vertices[i].colour, vertices[i].col);
		}
		std::vector<uint32_t> indices(indexCount);
		for (size_t i = 0; i < indexCount; i++)
			indices[i] = static_cast<uint32_t>(source->Indices[i]);
		renderer->DrawFilledRect(vertices.data(), vertices.size(), indices.data(), indices.size(), kProgramFilledAlphaBlend, "AbcUi filled");
		return;
	}

	std::vector<texturedrectvertex_t> vertices(vertexCount);
	for (size_t i = 0; i < vertexCount; i++) {
		const Rml::Vertex& v = source->Vertices[i];
		const Rml::Vector2f pos = applyTransform(v.position + translation);
		vertices[i].pos[0] = pos.x - (scissor ? static_cast<float>(ScissorRect.Left()) : 0.0f);
		vertices[i].pos[1] = pos.y - (scissor ? static_cast<float>(ScissorRect.Top()) : 0.0f);
		vertices[i].texcoord[0] = v.tex_coord.x;
		vertices[i].texcoord[1] = v.tex_coord.y;
		ToStraightColor(v.colour, vertices[i].col);
	}
	std::vector<uint32_t> indices(indexCount);
	for (size_t i = 0; i < indexCount; i++)
		indices[i] = static_cast<uint32_t>(source->Indices[i]);

	// Texture handles are surface texture ids (int namespace).
	const int textureId = static_cast<int>(texture);
	const uint64_t program = (textureId != 0 && AdditiveTextures.count(texture)) ? kProgramSpriteAdditive : kProgramAlphaBlend;
	renderer->DrawTexturedRect(textureId, vertices.data(), vertices.size(), indices.data(), indices.size(), program, "AbcUi");
}

void AbcRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
	delete reinterpret_cast<CompiledGeometry*>(geometry);
}

Rml::TextureHandle AbcRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
	// Sprite URIs ("spr://<hud.txt name>", "spricon://<atlas>/<l>,<t>,<r>,<b>",
	// "spralpha://...", "sprframe://...") resolve through the sprite data
	// layer, never the file interface (the scheme prefix also keeps Rml from
	// joining the document directory).
	if (source.rfind("spr://", 0) == 0 || source.rfind("spricon://", 0) == 0) {
		int width = 0, height = 0;
		const Rml::TextureHandle handle = sprites::textureForUri(source, width, height);
		if (handle) {
			dimensions.x = width;
			dimensions.y = height;
			// Old ImageSprPanel rendered HUD sprites kRenderTransAdd - mark
			// the handle so RenderGeometry picks the additive program.
			AdditiveTextures.insert(handle);
		}
		return handle;
	}
	if (source.rfind("avatar://", 0) == 0) {
		// avatar://<steamid64>[?gen] - scoreboard avatars (data::avatars);
		// the scoreboard bumps `gen` when an avatar lands so Rml re-resolves.
		const std::string body = source.substr(9);
		const uint64_t steamId = strtoull(body.c_str(), nullptr, 10);
		int width = 0, height = 0;
		const Rml::TextureHandle handle = avatars::textureFor(steamId, width, height, true);
		if (handle) {
			dimensions.x = width;
			dimensions.y = height;
		}
		return handle;
	}
	if (source.rfind("spralpha://", 0) == 0 || source.rfind("sprframe://", 0) == 0) {
		int width = 0, height = 0;
		const Rml::TextureHandle handle = sprites::textureForUri(source, width, height);
		if (handle) {
			dimensions.x = width;
			dimensions.y = height;
			// spralpha:// is the straight-alpha crop flavor; sprframe:// files
			// (water splash) are kRenderTransAdd like every HUD sprite.
			if (source.rfind("sprframe://", 0) == 0)
				AdditiveTextures.insert(handle);
		}
		return handle;
	}
	if (source.rfind("texadd://", 0) == 0) {
		// Mod-relative file loaded additive (old kRenderTransAdd TGA panels -
		// the pain indicator arc). Shares the FreeImage path below.
		const Rml::TextureHandle handle = loadFileTexture(dimensions, source.substr(9));
		if (handle)
			AdditiveTextures.insert(handle);
		return handle;
	}

	// `source` is the RML-referenced path (Rml joins the document directory);
	// resolve against the mod dir like every other abcui asset.
	return loadFileTexture(dimensions, source);
}

Rml::TextureHandle AbcRenderInterface::loadFileTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
	// `source` is the RML-referenced path (Rml joins the document directory);
	// resolve against the mod dir like every other abcui asset. Decoding goes
	// through the shared FreeImage loader - format is detected from the file
	// content, so RML sources stay extension-agnostic.
	framework::ImageLoader::DecodedImage image;
	if (!framework::ImageLoader::loadFromFile(abc::ModPath(source.c_str()).c_str(), image))
		return 0;

	const int textureId = UploadNewTexture(image.Pixels.data(), image.Width, image.Height);
	if (textureId == 0)
		return 0;
	dimensions = Rml::Vector2i(image.Width, image.Height);
	return static_cast<Rml::TextureHandle>(textureId);
}

Rml::TextureHandle AbcRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
	if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() < static_cast<size_t>(dimensions.x) * dimensions.y * 4)
		return 0;

	// RmlUi font/gradient atlases arrive premultiplied; straighten to match
	// our SRC_ALPHA blend (see header note).
	std::vector<unsigned char> pixels(source.begin(), source.end());
	UnpremultiplyInPlace(pixels.data(), static_cast<size_t>(dimensions.x) * dimensions.y);

	const int textureId = UploadNewTexture(pixels.data(), dimensions.x, dimensions.y);
	if (textureId == 0)
		return 0;
	return static_cast<Rml::TextureHandle>(textureId);
}

void AbcRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
	if (texture == 0)
		return;
	AdditiveTextures.erase(texture);
	vgui2ext::deleteTextureById(static_cast<int>(texture));
}

void AbcRenderInterface::clearAdditiveTextures() {
	AdditiveTextures.clear();
}

void AbcRenderInterface::EnableScissorRegion(bool enable) {
	ScissorEnabled = enable;
	if (!enable)
		ScissorRect = Rml::Rectanglei::MakeInvalid();
}

void AbcRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
	ScissorRect = region;
}

void AbcRenderInterface::SetTransform(const Rml::Matrix4f* transform) {
	TransformActive = transform != nullptr;
	if (transform)
		Transform = *transform;
}

Rml::Vector2f AbcRenderInterface::applyTransform(const Rml::Vector2f& point) const {
	if (!TransformActive)
		return point;
	const Rml::Vector4f transformed = Transform * Rml::Vector4f(point.x, point.y, 0.0f, 1.0f);
	return Rml::Vector2f(transformed.x, transformed.y);
}

}
