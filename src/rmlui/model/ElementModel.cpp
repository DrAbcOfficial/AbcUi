// New implementation; see ElementModel.h. Register before loading documents,
// render once at the end-of-frame UI boundary, never inside Rml geometry draws.
#include "ElementModel.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <abc/Console.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace rmlui::model {
namespace {
std::vector<ElementModel*>& elements() {
	static std::vector<ElementModel*> Elements;
	return Elements;
}
Rml::ElementInstancerGeneric<ElementModel> Instancer;
}

ElementModel::ElementModel(const Rml::String& Tag) : Element(Tag) {
	auto Child = Rml::Factory::InstanceElement(this, "img", "img", {});
	Image = Child.get();
	if (Image) {
		Image->SetProperty("width", "100%");
		Image->SetProperty("height", "100%");
		Image->SetProperty("display", "block");
		Image->SetProperty("pointer-events", "none");
		// FBO pixels have bottom-left origin, Rml images top-left.
		Image->SetProperty("transform", "scaleY(-1)");
		AppendChild(std::move(Child));
	}
	elements().push_back(this);
}

ElementModel::~ElementModel() {
	std::erase(elements(), this);
}

void ElementModel::OnAttributeChange(const Rml::ElementAttributes& Changed) {
	Element::OnAttributeChange(Changed);
	// Reparse from defaults so RemoveAttribute resets, rather than retaining
	// the removed value. Parsing errors do not leave a stale source visible.
	modelpreview::Options Updated;
	for (const auto& [Name, Value] : GetAttributes()) {
		if (!Updated.set(Name, Value.Get<Rml::String>()))
			abc::ConsoleWarn("[AbcUi model] invalid %s on #%s\n", Name.c_str(), GetId().c_str());
	}
	Config = std::move(Updated);
}

bool ElementModel::renderPreview() {
	if (!Image || !GetContext() || !IsVisible(true))
		return false;
	const auto Size = GetBox().GetSize(Rml::BoxArea::Content);
	if (!std::isfinite(Size.x) || !std::isfinite(Size.y) || Size.x <= 0 || Size.y <= 0)
		return false;
	const bool Ready = Target.render(Config,
		static_cast<int>(std::min(std::ceil(Size.x), 2048.0f)),
		static_cast<int>(std::min(std::ceil(Size.y), 2048.0f)));
	if (ImageReady == Ready)
		return false;
	ImageReady = Ready;
	if (Ready) {
		Image->SetAttribute("src", Target.uri());
		Image->SetProperty("visibility", "visible");
	} else {
		Image->SetProperty("visibility", "hidden");
	}
	return true;
}

void registerElement() {
	Rml::Factory::RegisterElementInstancer("model", &Instancer);
}

void renderPreviews() {
	Rml::Context* Context = nullptr;
	for (auto* Element : elements())
		if (Element->renderPreview())
			Context = Element->GetContext();
	if (Context)
		Context->Update();
}
}
