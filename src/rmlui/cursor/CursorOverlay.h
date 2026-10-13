#pragma once

// Menu pointer rendered through the existing Rml RenderInterface, after the
// context's documents. No OS cursor, assets, fonts, DOM, focus or input owner.
// The renderer is borrowed: reset() must precede its destruction.
#include <RmlUi/Core/RenderInterface.h>
#include <array>
#include <string_view>
#include <vector>

namespace rmlui {

class CursorOverlay {
public:
	CursorOverlay() = default;
	~CursorOverlay();
	CursorOverlay(const CursorOverlay&) = delete;
	CursorOverlay& operator=(const CursorOverlay&) = delete;

	void setShape(std::string_view Name);
	// Position and viewport use context pixels. Only the glyph uses dp scale.
	bool render(Rml::RenderInterface& Renderer, Rml::Vector2i Position,
		Rml::Vector2i Viewport, float DpScale, bool MenuOwnsInput);
	void reset();

private:
	enum class Shape { kArrow, kText, kCross, kNone };
	struct Mesh {
		// Rml's CompileGeometry contract permits borrowing these until release.
		std::vector<Rml::Vertex> Vertices;
		std::vector<int> Indices;
		Rml::CompiledGeometryHandle Handle = 0;
	};
	void releaseMeshes();
	void buildMesh(Mesh& Geometry);

	Rml::RenderInterface* Renderer_ = nullptr;
	std::array<Mesh, 3> Meshes;
	Shape Requested = Shape::kArrow;
	float Scale = 1.0f;
};

CursorOverlay& cursorOverlay();

}
