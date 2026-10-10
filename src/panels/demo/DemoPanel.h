#pragma once

// panels::DemoPanel - Phase 3 acceptance vehicle (plan 05 §9):
// one document demonstrating Chinese text, an image and scissor clipping,
// driven through the full chain (fonts -> DOM -> AbcRenderInterface ->
// IMetaRenderer). Not a real HUD panel; batch A panels supersede it.

#include "../../framework/HudPanel.h"

namespace panels {

class DemoPanel : public framework::HudPanel {
public:
	DemoPanel();

protected:
	void onDocumentReady() override;

private:
	double FrameCount = 0;
};

}
