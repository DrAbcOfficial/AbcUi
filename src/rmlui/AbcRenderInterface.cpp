// rmlui::AbcRenderInterface implementation - see AbcRenderInterface.h for the
// semantics (blend conversion, texture ids, viewport-as-scissor R-1 verdict).
#include <metahook.h>

#include "AbcRenderInterface.h"

#include <FreeImage.h>

#include <abc/Console.h>
#include <abc/Paths.h>
#include <abc/Render.h>
#include <abc/RenderCommon.h>

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

// Frees the FIBITMAP on scope exit (FreeImage has no RAII).
struct ScopedBitmap {
	FIBITMAP* Dib = nullptr;
	~ScopedBitmap() {
		if (Dib)
			FreeImage_Unload(Dib);
	}
};

int UploadNewTexture(const unsigned char* rgba, int width, int height) {
	const int id = vgui2ext::createTexture();
	if (id == 0) {
		static bool warned = false;
		if (!warned) {
			abc::ConsoleWarn("[AbcUi] texture upload unavailable (VGUI2Extension surface missing)\n");
			warned = true;
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
	const bool scissor = ScissorEnabled && ScissorRect.Valid() && ScissorRect.Width() > 0 && ScissorRect.Height() > 0;
	if (scissor)
		translation -= Rml::Vector2f(static_cast<float>(ScissorRect.Left()), static_cast<float>(ScissorRect.Top()));

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
			vertices[i].pos[0] = source->Vertices[i].position.x + translation.x;
			vertices[i].pos[1] = source->Vertices[i].position.y + translation.y;
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
		vertices[i].pos[0] = v.position.x + translation.x;
		vertices[i].pos[1] = v.position.y + translation.y;
		vertices[i].texcoord[0] = v.tex_coord.x;
		vertices[i].texcoord[1] = v.tex_coord.y;
		ToStraightColor(v.colour, vertices[i].col);
	}
	std::vector<uint32_t> indices(indexCount);
	for (size_t i = 0; i < indexCount; i++)
		indices[i] = static_cast<uint32_t>(source->Indices[i]);

	// Texture handles are surface texture ids (int namespace).
	const int textureId = static_cast<int>(texture);
	renderer->DrawTexturedRect(textureId, vertices.data(), vertices.size(), indices.data(), indices.size(), kProgramAlphaBlend, "AbcUi");
}

void AbcRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
	delete reinterpret_cast<CompiledGeometry*>(geometry);
}

Rml::TextureHandle AbcRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
	// `source` is the RML-referenced path (Rml joins the document directory);
	// resolve against the mod dir like every other abcui asset.
	const std::string path = abc::ModPath(source.c_str());

	const FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path.c_str(), 0);
	if (format == FIF_UNKNOWN)
		return 0;
	ScopedBitmap bitmap;
	bitmap.Dib = FreeImage_Load(format, path.c_str());
	if (!bitmap.Dib)
		return 0;

	FIBITMAP* rgba = FreeImage_ConvertTo32Bits(bitmap.Dib);
	if (!rgba)
		return 0;
	bitmap.Dib = rgba; // take ownership for unified unload

	const int width = FreeImage_GetWidth(rgba);
	const int height = FreeImage_GetHeight(rgba);
	if (width <= 0 || height <= 0)
		return 0;

	// FreeImage scans bottom-up BGRA; upload wants top-down RGBA.
	FreeImage_FlipVertical(rgba);
	const int stride = width * 4;
	std::vector<unsigned char> pixels(static_cast<size_t>(stride) * height);
	for (int y = 0; y < height; y++) {
		const BYTE* src = FreeImage_GetScanLine(rgba, y);
		unsigned char* dst = pixels.data() + static_cast<size_t>(y) * stride;
		for (int x = 0; x < width; x++) {
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}

	const int textureId = UploadNewTexture(pixels.data(), width, height);
	if (textureId == 0)
		return 0;
	dimensions = Rml::Vector2i(width, height);
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
	vgui2ext::deleteTextureById(static_cast<int>(texture));
}

void AbcRenderInterface::EnableScissorRegion(bool enable) {
	ScissorEnabled = enable;
	if (!enable)
		ScissorRect = Rml::Rectanglei::MakeInvalid();
}

void AbcRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
	ScissorRect = region;
}

}
