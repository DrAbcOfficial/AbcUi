#pragma once

// New implementation. Engine-independent <model> attribute validation.
// Target consumes the validated options; ElementModel supplies string values.
#include <IModelPreviewRenderer.h>
#include <array>
#include <string>
#include <string_view>

namespace modelpreview {

struct Options {
	ModelPreviewView View;
	std::string Source;
	std::array<float, 3> Origin{};
	std::array<float, 3> Angles{};
	std::array<int, 3> RenderColor{255, 255, 255};
	std::array<int, 4> Controllers{};
	std::array<int, 2> Blending{};
	int Mouth = 0;
	int ColorMap = 0;
	int RenderMode = 0;
	int RenderFx = 0;
	int RenderAmount = 255;
	int Sequence = 0;
	int Skin = 0;
	int Body = 0;
	float Frame = 0;
	float FrameRate = 1;
	float Scale = 1;
	bool Animate = true;

	// Unknown DOM attributes (id/class/style) are not errors. Invalid known
	// attributes leave the previous value unchanged and return false.
	bool set(std::string_view Name, std::string_view Value);
	static bool isModelPath(std::string_view Path);
};

}
