// framework::Theme implementation - see Theme.h.
#include "Theme.h"

#include <RmlUi/Core/Context.h>

#include <abc/Render.h>

#include <algorithm>

namespace framework {

namespace Theme {

namespace {
float CurrentScale = 1.0f;
}

void applyScale(Rml::Context* context) {
	const float height = abc::MetaRenderer()
		? static_cast<float>(abc::MetaRenderer()->GetSwapChainHeight())
		: 480.0f;
	CurrentScale = std::clamp(height / 480.0f, 0.25f, 8.0f);
	if (context)
		context->SetDensityIndependentPixelRatio(CurrentScale);
}

float scale() {
	return CurrentScale;
}

}

}
