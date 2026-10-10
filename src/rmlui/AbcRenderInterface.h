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

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

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

private:
	struct CompiledGeometry {
		std::vector<Rml::Vertex> Vertices;
		std::vector<int> Indices;
	};

	bool ScissorEnabled = false;
	Rml::Rectanglei ScissorRect = Rml::Rectanglei::MakeInvalid();
	// Mirrors what the renderer is currently set up for, so consecutive
	// geometries under one scissor state skip redundant Set2D calls.
	bool TwoDActive = false;
	bool AppliedScissor = false;
	Rml::Rectanglei AppliedScissorRect = Rml::Rectanglei::MakeInvalid();
};

}
