// Asset-independent, high-contrast cursor geometry. Kept separate from input
// routing and platform bindings so the actual drawing/lifetime is testable.
#include "CursorOverlay.h"

#include <algorithm>
#include <cmath>

namespace rmlui {

CursorOverlay::~CursorOverlay() {
	reset();
}

void CursorOverlay::setShape(std::string_view Name) {
	if (Name == "none")
		Requested = Shape::kNone;
	else if (Name == "text")
		Requested = Shape::kText;
	else if (Name == "cross" || Name == "crosshair" || Name == "move")
		Requested = Shape::kCross;
	else
		Requested = Shape::kArrow; // auto/arrow/pointer and unsupported names
}

void CursorOverlay::releaseMeshes() {
	for (Mesh& Geometry : Meshes) {
		if (Renderer_ && Geometry.Handle)
			Renderer_->ReleaseGeometry(Geometry.Handle);
		Geometry = Mesh{};
	}
	Renderer_ = nullptr;
}

void CursorOverlay::reset() {
	releaseMeshes();
	Requested = Shape::kArrow;
	Scale = 1.0f;
}

void CursorOverlay::buildMesh(Mesh& Geometry) {
	const Rml::ColourbPremultiplied Dark(16, 20, 24, 255);
	const Rml::ColourbPremultiplied Light(255, 255, 255, 255);
	const auto Triangle = [&](Rml::Vector2f A, Rml::Vector2f B, Rml::Vector2f C,
		Rml::ColourbPremultiplied Color) {
		for (const Rml::Vector2f Point : {A, B, C}) {
			Geometry.Indices.push_back(static_cast<int>(Geometry.Vertices.size()));
			Geometry.Vertices.push_back({Point * Scale, Color, {}});
		}
	};
	const auto Quad = [&](float X, float Y, float W, float H, Rml::ColourbPremultiplied Color) {
		Triangle({X, Y}, {X + W, Y}, {X + W, Y + H}, Color);
		Triangle({X, Y}, {X + W, Y + H}, {X, Y + H}, Color);
	};
	if (Requested == Shape::kText) {
		Quad(-2, -9, 4, 18, Dark);
		Quad(-5, -10, 10, 3, Dark);
		Quad(-5, 7, 10, 3, Dark);
		Quad(-0.5f, -8, 1, 16, Light);
		Quad(-4, -9, 8, 1, Light);
		Quad(-4, 8, 8, 1, Light);
	} else if (Requested == Shape::kCross) {
		Quad(-8, -2, 16, 4, Dark);
		Quad(-2, -8, 4, 16, Dark);
		Quad(-7, -0.5f, 14, 1, Light);
		Quad(-0.5f, -7, 1, 14, Light);
	} else {
		// Hotspot is the arrow tip at (0,0). Outline first, then white fill.
		Triangle({0, 0}, {0, 19}, {14, 13}, Dark);
		Triangle({4, 12}, {8, 23}, {11, 21.5f}, Dark);
		Triangle({4, 12}, {11, 21.5f}, {7, 12}, Dark);
		Triangle({1.5f, 3.5f}, {1.5f, 16}, {10.5f, 12}, Light);
		Triangle({5.5f, 12}, {8.5f, 21}, {9.5f, 20.5f}, Light);
		Triangle({5.5f, 12}, {9.5f, 20.5f}, {6.5f, 12}, Light);
	}
}

bool CursorOverlay::render(Rml::RenderInterface& Renderer, Rml::Vector2i Position,
	Rml::Vector2i Viewport, float DpScale, bool MenuOwnsInput) {
	if (!MenuOwnsInput || Requested == Shape::kNone || Position.x < 0 || Position.y < 0
		|| Position.x >= Viewport.x || Position.y >= Viewport.y)
		return false;
	const float NextScale = std::isfinite(DpScale) && DpScale > 0
		? std::clamp(DpScale, 0.25f, 8.0f) : 1.0f;
	if (Renderer_ != &Renderer || NextScale != Scale) {
		releaseMeshes();
		Renderer_ = &Renderer;
		Scale = NextScale;
	}
	Mesh& Geometry = Meshes[static_cast<std::size_t>(Requested)];
	if (Geometry.Vertices.empty())
		buildMesh(Geometry);
	if (!Geometry.Handle)
		Geometry.Handle = Renderer.CompileGeometry(Geometry.Vertices, Geometry.Indices);
	if (!Geometry.Handle)
		return false;
	// Context::Render resets the Rml state before this final overlay. Reassert
	// identity/no clipping so a scrolled/transformed page cannot hide the tip.
	Renderer.SetTransform(nullptr);
	Renderer.EnableScissorRegion(false);
	Renderer.RenderGeometry(Geometry.Handle,
		{static_cast<float>(Position.x), static_cast<float>(Position.y)}, 0);
	return true;
}

CursorOverlay& cursorOverlay() {
	static CursorOverlay Cursor;
	return Cursor;
}

}
