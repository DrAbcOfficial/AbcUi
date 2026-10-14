#pragma once

// New custom Rml element. Owns a Target; an internal <img> handles clipping,
// transforms and opacity through the ordinary Rml renderer.
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>
#include "../../services/modelpreview/Target.h"

namespace Rml {
class ElementDocument;
class Event;
}

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
	void OnUpdate() override;
private:
	// Pointer drag rotation (settings' multiplayer preview): while the primary
	// button is held over the element, horizontal motion turns the model and
	// vertical motion tilts it (clamped) - the CS:GO-style inspection gesture.
	void beginDrag(int x, int y);
	void updateDrag(int x, int y);
	void endDrag();
	// Document-scoped move/release listeners: a drag must survive the pointer
	// leaving the element box (Rml only dispatches mousemove to the hover
	// chain), so the element listens on its owner document while dragging.
	class DragListener final : public Rml::EventListener {
	public:
		explicit DragListener(ElementModel* owner) : Owner(owner) {}
		void ProcessEvent(Rml::Event& event) override;

	private:
		ElementModel* Owner;
	};

	modelpreview::Options Config;
	modelpreview::Target Target;
	DragListener Listener;
	Rml::Element* Image = nullptr;
	Rml::ElementDocument* ListenedDocument = nullptr;
	bool ImageReady = false;
	bool Dragging = false;
	int DragStartX = 0;
	int DragStartY = 0;
	float DragYaw = 0.0f;
	float DragPitch = 0.0f;
	// The config's own angles are the base; the drag adds to them.
	float BaseYaw = 0.0f;
	float BasePitch = 0.0f;
};

void registerElement();
void renderPreviews();
}
