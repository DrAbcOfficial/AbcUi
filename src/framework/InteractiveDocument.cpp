// framework::InteractiveDocument implementation - see InteractiveDocument.h.
#include "InteractiveDocument.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

#include <abc/Console.h>

#include <cstdio>

namespace framework {

namespace {

// Emergency page (T05 step 5): inline RML so it never depends on the
// external menu assets it exists to replace. Deliberately plain: no
// localized #tokens (the lang table is an external asset too), no image
// sources, dp units only - the context density ratio (Theme::applyScale)
// scales it like every other document, no second sizing logic. The %s is the
// owner's document name so context->GetDocument() keeps resolving it. Char
// array, not pointer: sizeof below is the load buffer's sizing contract.
constexpr char kEmergencyRml[] =
	"<rml>"
	"<head>"
	"<style>"
	"	body { background-color: #000000; }"
	"	div { display: block; }"
	"	#panel { position: absolute; left: 60dp; top: 60dp;"
	"		width: 400dp; background-color: #101418; padding: 20dp;"
	"		border: 1dp #3a4148; }"
	"	h1 { display: block; font-size: 14dp; font-weight: bold;"
	"		color: #ff5500; }"
	"	p { display: block; font-size: 10dp; color: #dedede; }"
	"</style>"
	"</head>"
	"<body id=\"%s\">"
	"<div id=\"panel\">"
	"<h1>AbcUi emergency mode</h1>"
	"<p>Menu resources are missing or broken.</p>"
	"<p>Check the console log; the UI stays out of the way until they are fixed.</p>"
	"</div>"
	"</body>"
	"</rml>";

} // namespace

InteractiveDocument::InteractiveDocument(const char* name, const char* documentPath)
	: Name(name), DocumentPath(documentPath) {}

InteractiveDocument::~InteractiveDocument() {
	close();
}

InteractiveDocument::LoadOutcome InteractiveDocument::load(Rml::Context* context) {
	if (!context)
		return LoadOutcome::kFailed;
	if (Document)
		return Degraded ? LoadOutcome::kEmergencyFallback : LoadOutcome::kLoaded;

	Document = context->LoadDocument(DocumentPath);
	if (Document) {
		Degraded = false;
		onDocumentReady();
		return LoadOutcome::kLoaded;
	}

	// The missing main document is a clear, logged outcome - never silent.
	abc::ConsoleWarn("[AbcUi] interactive document \"%s\": failed to load %s, falling back to emergency page\n",
		Name.c_str(), DocumentPath.c_str());
	char rml[sizeof(kEmergencyRml) + 64];
	std::snprintf(rml, sizeof(rml), kEmergencyRml, Name.c_str());
	Document = context->LoadDocumentFromMemory(rml);
	if (!Document)
		return LoadOutcome::kFailed;
	Degraded = true;
	return LoadOutcome::kEmergencyFallback;
}

void InteractiveDocument::show() {
	if (!Document || Document->IsVisible())
		return;
	// FocusFlag::Auto + the document's autofocus attribute: first show lands
	// on the page's primary element, re-shows land the same way. Modal stays
	// off - input ownership arbitration is T06, not a document property.
	Document->Show(Rml::ModalFlag::None, Rml::FocusFlag::Auto);
	// Show() does not order the context's document stack; interactive
	// windows own their z-order explicitly.
	Document->PullToFront();
}

void InteractiveDocument::hide() {
	if (Document)
		Document->Hide();
}

void InteractiveDocument::close() {
	if (!Document)
		return;
	// Order is the contract: no listener can observe the DOM teardown, and
	// no binding outlives the elements they hang on.
	for (const ListenerBinding& binding : Listeners)
		binding.Target->RemoveEventListener(binding.Event, binding.Listener, binding.Capture);
	Listeners.clear();
	Document->Close();
	Document = nullptr;
	Degraded = false;
}

bool InteractiveDocument::isVisible() const {
	return Document && Document->IsVisible();
}

void InteractiveDocument::addListener(Rml::Element* target, const char* event,
	Rml::EventListener* listener, bool capturePhase) {
	if (!target || !listener)
		return;
	target->AddEventListener(event, listener, capturePhase);
	Listeners.push_back({target, event, listener, capturePhase});
}

Rml::Element* InteractiveDocument::element(const char* id) {
	Rml::Element* found = Document ? Document->GetElementById(id) : nullptr;
	if (!found)
		abc::ConsoleWarn("[AbcUi] interactive document \"%s\": element \"%s\" missing\n", Name.c_str(), id);
	return found;
}

}
