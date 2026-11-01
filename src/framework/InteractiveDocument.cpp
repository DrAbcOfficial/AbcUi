// framework::InteractiveDocument implementation - see InteractiveDocument.h.
#include "InteractiveDocument.h"
#include "MenuBackdrop.h"

#include "DocumentLoadGuard.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

#include <abc/Console.h>

#include <cstdio>
#include <utility>

namespace framework {

InteractiveDocument::InteractiveDocument(const char *name,
                                         const char *documentPath,
                                         bool withBackdrop, int zIndex)
    : Name(name), DocumentPath(documentPath), WithBackdrop(withBackdrop),
      ZIndex(zIndex) {}

InteractiveDocument::~InteractiveDocument() {
  close();
}

InteractiveDocument::LoadOutcome
InteractiveDocument::load(Rml::Context *context) {
  if (!context)
    return LoadOutcome::kFailed;
  if (Document)
    return Degraded ? LoadOutcome::kEmergencyFallback : LoadOutcome::kLoaded;

  // T21: bracket the main-document load (see DocumentLoadGuard.h). RmlUi's
  // parser is lenient - a syntax-broken file yields a PARTIALLY parsed,
  // non-null document plus "XML parse error" warnings; appending that DOM
  // would present an empty black page (live-caught in the fault matrix).
  // A failure signal inside the load window closes the partial document
  // and redirects into the emergency path below.
  const uint64_t loadMark = documentload::begin();
  Document = context->LoadDocument(DocumentPath);
  if (Document && documentload::failedSince(loadMark)) {
    abc::ConsoleWarn("[AbcUi] interactive document \"%s\": %s failed to parse "
                     "cleanly, discarding partial document\n",
                     Name.c_str(), DocumentPath.c_str());
    Document->Close();
    Document = nullptr;
  }
  if (Document) {
    Degraded = false;
    applyZIndex();
    beginLoaded();
    // Backdrop before the page's own setup: the page's onDocumentReady may
    // resolve or reorder nodes, and the backdrop must already sit behind
    // them (paint order is DOM order).
    if (WithBackdrop)
      menubackdrop::attach(Document);
    onDocumentReady();
    return LoadOutcome::kLoaded;
  }

  // The missing/broken main document is a clear, logged outcome - never silent.
  abc::ConsoleWarn("[AbcUi] interactive document \"%s\": failed to load %s, "
                   "falling back to emergency page\n",
                   Name.c_str(), DocumentPath.c_str());
  const char *path = recoveryPath();
  if (!path || !*path)
    return LoadOutcome::kFailed;
  const uint64_t recoveryMark = documentload::begin();
  Document = context->LoadDocument(path);
  if (Document && documentload::failedSince(recoveryMark)) {
    Document->Close();
    Document = nullptr;
  }
  if (!Document)
    return LoadOutcome::kFailed;
  Degraded = true;
  RetryRequested = false;
  applyZIndex();
  beginLoaded();
  onRecoveryReady();
  return LoadOutcome::kEmergencyFallback;
}

InteractiveDocument::LoadOutcome
InteractiveDocument::retry(Rml::Context *context) {
  if (Document && !Degraded)
    return LoadOutcome::kLoaded; // healthy: nothing to recover
  close();
  return load(context);
}

void InteractiveDocument::show() { show(Rml::FocusFlag::Auto); }

void InteractiveDocument::show(Rml::FocusFlag focus) {
  if (!Document || Presented)
    return;
  Presented = true;
  // A document still dissolving out (see hide()) is already on screen; only a
  // finished hide needs Rml's Show(). Skipping it also keeps the focus where
  // it is, so reversing a fade never churns the keyboard owner.
  if (!Document->IsVisible()) {
    // FocusFlag::Auto + the document's autofocus attribute: first show lands
    // on the page's primary element, re-shows land the same way. Modal stays
    // off - input ownership arbitration is T06, not a document property.
    Document->Show(Rml::ModalFlag::None, focus);
  }
  // Show() does not order the context's document stack; interactive windows
  // own their z-order explicitly. Also when the page reversed its own
  // fade-out: a page that comes back must come back to the front.
  Document->PullToFront();
  startFade(1.0f, kFadeInSeconds, false);
}

void InteractiveDocument::hide() {
  // Idempotent like show(): Rml's Hide re-dispatches the hide event and
  // unfocuses the document every call, which would churn focus each time a
  // sync re-hides an already hidden document.
  if (!Document || !Presented)
    return;
  // The logical truth flips now; the pixels follow over kFadeOutSeconds and
  // pump() applies the physical Hide() once the page is fully transparent.
  Presented = false;
  startFade(0.0f, kFadeOutSeconds, true);
}

void InteractiveDocument::pump(double frameTime) {
  if (!Document || !FadeActive)
    return;
  if (FadeDuration <= 0.0) {
    writeOpacity(FadeTo);
    finishFade();
    return;
  }
  FadeElapsed += frameTime;
  if (FadeElapsed >= FadeDuration) {
    writeOpacity(FadeTo);
    finishFade();
    return;
  }
  // Ease-out over the ramp: the page leaves/arrives quickly, then settles.
  const float progress = static_cast<float>(FadeElapsed / FadeDuration);
  const float remaining = 1.0f - progress;
  const float eased = 1.0f - remaining * remaining;
  writeOpacity(FadeFrom + (FadeTo - FadeFrom) * eased);
}

void InteractiveDocument::startFade(float target, double seconds,
                                    bool hideWhenDone) {
  FadeFrom = FadeOpacity;
  FadeTo = target;
  FadeElapsed = 0.0;
  FadeDuration = seconds;
  HideWhenFadeDone = hideWhenDone;
  FadeActive = true;
  if (FadeFrom == FadeTo) // nothing to ramp (e.g. hidden before it drew)
    finishFade();
}

void InteractiveDocument::finishFade() {
  FadeActive = false;
  if (HideWhenFadeDone) {
    HideWhenFadeDone = false;
    // Keep the inline zero: the next show() ramps up from transparent.
    if (Document)
      Document->Hide();
    return;
  }
  // Back to the stylesheet's own opacity: an open page carries no residue.
  if (Document) {
    Document->RemoveProperty("opacity");
    FadeOpacity = 1.0f;
  }
}

void InteractiveDocument::writeOpacity(float opacity) {
  FadeOpacity = opacity;
  if (!Document)
    return;
  char value[16];
  std::snprintf(value, sizeof(value), "%.3f", opacity);
  Document->SetProperty("opacity", value);
}

void InteractiveDocument::close() {
  if (!Document)
    return;
  // Invalidate ownership before callbacks: OnDetach/Close can re-enter.
  Rml::ElementDocument *ClosingDocument = Document;
  Document = nullptr;
  const bool WasDegraded = Degraded;
  Degraded = false;
  RetryRequested = false;
  // A fade in flight dies with the document; the next load() starts
  // transparent again.
  Presented = false;
  FadeActive = false;
  HideWhenFadeDone = false;
  FadeOpacity = 0.0f;
  if (WasDegraded)
    onRecoveryClose();
  auto Bindings = std::move(Listeners);
  Listeners.clear();
  for (const ListenerBinding &Binding : Bindings) {
    if (Rml::Element *Target = Binding.Target.get())
      Target->RemoveEventListener(Binding.Event, Binding.Listener,
                                  Binding.Capture);
  }
  ClosingDocument->Close();
}

bool InteractiveDocument::isVisible() const { return Document && Presented; }

void InteractiveDocument::beginLoaded() {
  // A freshly loaded page starts fully transparent and hidden: the first
  // show() ramps it in (writeOpacity is the only place that touches the
  // inline opacity, so load and fade can never disagree).
  Presented = false;
  FadeActive = false;
  HideWhenFadeDone = false;
  FadeFrom = 0.0f;
  FadeTo = 0.0f;
  FadeElapsed = 0.0;
  FadeDuration = 0.0;
  writeOpacity(0.0f);
}

void InteractiveDocument::applyZIndex() {
  // Cross-document order only (see the tier constants in the header): RmlUi
  // paints and hit-tests the context root's stacking context by z-index
  // first, so a positive tier covers every default document. Set once at
  // load, never toggled - a hidden document is not in the stacking context,
  // so the visible order returns on its own when the layer hides.
  if (Document && ZIndex != 0)
    Document->SetProperty("z-index", std::to_string(ZIndex));
}

void InteractiveDocument::addListener(Rml::Element *target, const char *event,
                                      Rml::EventListener *listener,
                                      bool capturePhase) {
  if (!target || !listener)
    return;
  target->AddEventListener(event, listener, capturePhase);
  Listeners.push_back(
      {target->GetObserverPtr(), event, listener, capturePhase});
}

Rml::Element *InteractiveDocument::element(const char *id) {
  Rml::Element *found = Document ? Document->GetElementById(id) : nullptr;
  if (!found)
    abc::ConsoleWarn(
        "[AbcUi] interactive document \"%s\": element \"%s\" missing\n",
        Name.c_str(), id);
  return found;
}

} // namespace framework
