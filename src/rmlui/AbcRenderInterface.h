#pragma once

// rmlui::AbcRenderInterface - Rml::RenderInterface implemented over
// IMetaRenderer (Renderer.dll). Plan 05 §3.2.
//
// Seam rules (09 §9.2): every GPU command goes through IMetaRenderer or the
// VGUI2 surface texture channel (vgui2ext helpers); no OpenGL calls and no
// engine headers leak past this header (only Rml + abc types).
//
// Key semantics:
// - Blending: the renderer's textured/filled rect paths only offer straight
//   alpha (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). RmlUi hands out premultiplied
//   vertex colours and premultiplied font atlas bytes, so both are converted
//   to straight alpha here (un-premultiply; alpha 0 clamps to black).
// - Sprite textures (spr://, spricon://) draw with the alpha-based additive
//   program (SRC_ALPHA, ONE = kRenderTransAdd "src*a + dest") - the old
//   ImageSprPanel drew every HUD sprite icon that way, which both removes
//   the sprite's black background and lets the Rml opacity transition ride
//   the same alpha term (old panel-alpha fade).
// - Texture handles are VGUI2 surface texture ids (CreateNewTextureID /
//   DrawSetTextureRGBA), the same namespace IMetaRenderer draws from.
// - Scissor (Spike R-1): IMetaRenderer has no glScissor equivalent, but
//   Set2DEx(x, y, w, h) installs viewport = rect AND an ortho projection
//   mapping local (0..w, 0..h) into it; rasterization outside a viewport is
//   clipped, so the scissor region is realized as Set2DEx + a vertex offset
//   by the region origin. Verified against Renderer pinned sources
//   (GL_Set2DEx in gl_rmain.cpp); see plans_docs/00 D-12 for the R-1 verdict.
// - 2D state ownership: RenderGeometry installs Set2D/Set2DEx lazily per
//   scissor state; beginFrame()/endFrame() bracket a render pass (endFrame
//   pops the matrix stack). Called by RmlUiModule around Context::Render().
// - Transforms: Rml element transforms (rotate etc., batch B Indicator) pass
//   through SetTransform as a matrix in the same screen space as the compiled
//   vertices; applied CPU-side in RenderGeometry as M·(v + translation) (the
//   GL2 sample order: translate first, then the loaded matrix). Reset by
//   RmlUi's RenderManager identity reset whenever the next element is
//   untransformed, so nothing lingers across the frame.
// - Extra texture URIs handled here:
//     texadd://<mod-relative path> -> file loaded additive (old
//     kRenderTransAdd TGA panels: the pain indicator).

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rmlui {

class AbcRenderInterface : public Rml::RenderInterface {
public:
	AbcRenderInterface() = default;
	~AbcRenderInterface() override;

	// Bracket one render pass (see header note on 2D state ownership).
	void beginFrame();
	void endFrame();

	// Rml::RenderInterface - required set.
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;
	void SetTransform(const Rml::Matrix4f* transform) override;

	// Forgets the additive-texture marks (SpriteResource bulk-deletes its
	// textures outside RmlUi on shutdown - stale ids must not tint regular
	// textures that later reuse them).
	void clearAdditiveTextures();

	// Externally-owned live texture (batch D - the radar FBO). Requests whose
	// source starts with `uriPrefix` resolve to `texture` (never deleted by
	// ReleaseTexture; the owner frees it), carry the given pixel dimensions,
	// and draw through *maskSlot as a mask texture whenever it is non-zero
	// (radar shape select) - the old CRadarMapImage::Paint semantics. The
	// query suffix on the URI is a cache-bust only (the FBO texture id
	// changes when the engine regenerates frame buffers; the owner bumps the
	// suffix and re-registers).
	void registerExternalTexture(const Rml::String& uriPrefix, Rml::TextureHandle texture,
		int width, int height, const int* maskSlot);
	void unregisterExternalTexture(const Rml::String& uriPrefix);

private:
	struct CompiledGeometry {
		std::vector<Rml::Vertex> Vertices;
		std::vector<int> Indices;
	};

	// Applies the active Rml transform (if any) to a screen-space point.
	Rml::Vector2f applyTransform(const Rml::Vector2f& point) const;
	// File decode behind both the plain-path and texadd:// calls goes
	// through framework::ImageLoader (FreeImage); this wrapper uploads the
	// result to the VGUI2 surface texture channel.
	Rml::TextureHandle loadFileTexture(Rml::Vector2i& dimensions, const Rml::String& source);

	// External live textures (see registerExternalTexture) plus the handles
	// this interface created itself through the VGUI surface channel: only
	// owned handles may be deleted on ReleaseTexture (external ones - the
	// radar FBO - belong to their owner).
	struct ExternalTexture {
		Rml::TextureHandle Texture = 0;
		int Width = 0;
		int Height = 0;
		const int* MaskSlot = nullptr;
	};
	std::unordered_map<Rml::String, ExternalTexture> ExternalTextures;
	std::unordered_set<Rml::TextureHandle> OwnedTextures;

	bool ScissorEnabled = false;
	Rml::Rectanglei ScissorRect = Rml::Rectanglei::MakeInvalid();
	// Mirrors what the renderer is currently set up for, so consecutive
	// geometries under one scissor state skip redundant Set2D calls.
	bool TwoDActive = false;
	bool AppliedScissor = false;
	Rml::Rectanglei AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
	// Sprite textures draw additive (see header note).
	std::unordered_set<Rml::TextureHandle> AdditiveTextures;
	// Current element transform (null = identity); RmlUi only calls
	// SetTransform when the value actually changes (RenderManager).
	Rml::Matrix4f Transform = Rml::Matrix4f::Identity();
	bool TransformActive = false;
};

}
