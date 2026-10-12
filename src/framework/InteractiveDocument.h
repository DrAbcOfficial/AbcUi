#pragma once

// framework::InteractiveDocument - owner of one interactive (non-HUD) Rml
// document on the shared abcui context (T05).
//
// The passive HUD panels load through HudPanel: pointer-events none,
// FocusFlag::None - they must never intercept hit testing or steal the
// context focus. Interactive documents are the complement: shown with
// document focus (autofocus rides Rml's FocusFlag::Auto), pulled to the
// front of the context z-order, hit-testable, and closable.
//
// Listener lifetime is the owner's job: every listener attached through
// addListener() is recorded and detached again in close() BEFORE the
// document is closed, so no handler can ever fire on (or after) DOM
// teardown. The same contract keeps borrowed element pointers safe: after
// close() the document pointer is null and nothing reachable references the
// dead DOM.
//
// Render path: documents render through the existing single-context frame
// path (rmlui::onFrame/onRedraw -> Context::Update, OnRenderEndFrame ->
// Context::Render). This class adds no context, renderer or second render
// pass (T05 forbidden list).

#include <RmlUi/Core/ElementDocument.h>

#include <string>
#include <vector>

namespace Rml {
class Context;
class Element;
class EventListener;
}

namespace framework {

class InteractiveDocument {
public:
	// Explicit outcome of load() (T05 step 5): a missing main document is a
	// clear result, never a silent success. The full strict-failure policy is
	// T21; this only carries the emergency-document entry.
	enum class LoadOutcome {
		kLoaded,            // main document loaded from its file path
		kEmergencyFallback, // main document missing/broken; built-in emergency
							// page loaded instead (inline RML, no external
							// menu assets)
		kFailed,            // even the emergency page failed (Rml core broken);
							// caller escalates, nothing is loaded
	};

	InteractiveDocument(const char* name, const char* documentPath);
	virtual ~InteractiveDocument();

	InteractiveDocument(const InteractiveDocument&) = delete;
	InteractiveDocument& operator=(const InteractiveDocument&) = delete;

	// Loads the main document; on failure loads the built-in emergency page
	// instead (degraded mode, isDegraded()). Safe to call again after
	// close(); calling on an already-loaded document is a no-op returning the
	// outcome of the earlier load.
	LoadOutcome load(Rml::Context* context);

	// Show: visible, context focus (autofocus element wins), front of the
	// context z-order. Idempotent while visible: a repeated show neither
	// re-focuses nor re-orders (HudPanel::show's steal-the-focus guard, the
	// interactive counterpart).
	void show();
	// Hide: visibility only. Listener bindings survive hide/show cycles.
	// Context-focus release when no other document exists is RmlUi's own
	// behavior (UnfocusDocument keeps the pointer with nothing to hand to);
	// real focus-release semantics are T06's input ownership.
	void hide();
	// Close: detach every listener, then close the document. After close()
	// the object is back to the pre-load state and may load() again.
	void close();

	bool isLoaded() const { return Document != nullptr; }
	// True when the loaded document is the emergency fallback, not the main
	// document (callers surface this instead of pretending normal service).
	bool isDegraded() const { return Document != nullptr && Degraded; }
	bool isVisible() const;
	Rml::ElementDocument* document() const { return Document; }
	const std::string& getName() const { return Name; }

protected:
	// Post-load hook for the real document: resolve elements and attach
	// listeners through addListener(). Never fires for the emergency page
	// (its DOM has none of the page's elements; degrade state is queryable).
	virtual void onDocumentReady() {}

	// Owner-tracked listener attach; detached again before Close() runs.
	void addListener(Rml::Element* target, const char* event,
		Rml::EventListener* listener, bool capturePhase = false);

	// Common lookup with a warn-on-missing guard (HudPanel parity).
	Rml::Element* element(const char* id);

private:
	struct ListenerBinding {
		Rml::Element* Target;
		std::string Event;
		Rml::EventListener* Listener;
		bool Capture;
	};

	const std::string Name;
	const std::string DocumentPath;
	Rml::ElementDocument* Document = nullptr;
	bool Degraded = false;
	std::vector<ListenerBinding> Listeners;
};

}
