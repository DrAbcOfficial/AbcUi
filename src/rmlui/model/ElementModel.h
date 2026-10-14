#pragma once

// New custom Rml element. Owns a Target; an internal <img> handles clipping,
// transforms and opacity through the ordinary Rml renderer.
#include <RmlUi/Core/Element.h>
#include "../../services/modelpreview/Target.h"

namespace rmlui::model {
class ElementModel : public Rml::Element {
public:
	explicit ElementModel(const Rml::String& Tag);
	~ElementModel() override;
	bool renderPreview();
	unsigned int texture() const { return Target.texture(); }
	unsigned int frames() const { return Target.frames(); }
protected:
	void OnAttributeChange(const Rml::ElementAttributes& Changed) override;
private:
	modelpreview::Options Config;
	modelpreview::Target Target;
	Rml::Element* Image = nullptr;
	bool ImageReady = false;
};

void registerElement();
void renderPreviews();
}
