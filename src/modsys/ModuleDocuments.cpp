// framework::module_documents implementation - see ModuleDocuments.h.
#include "ModuleDocuments.h"
#include "ModuleResources.h"

#include <abc/Console.h>

#include <RmlUi/Core/Context.h>

#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../framework/InteractiveDocument.h"
#include "../services/menu/MenuHost.h"

namespace framework {
namespace module_documents {
namespace {

class ModuleDocument final
    : public InteractiveDocument,
      public std::enable_shared_from_this<ModuleDocument> {
public:
  explicit ModuleDocument(const AbcUiDocumentDesc &desc)
      : InteractiveDocument(desc.name, desc.path,
                            (desc.flags & ABCUI_DOC_FLAG_BACKDROP) != 0,
                            desc.zIndex),
        Desc(desc) {
    // The built-in emergency page's two affordances: Quit exits through the
    // menu host's confirmed path (a fault-time exit cannot depend on a page
    // that may be the broken one); Retry is acted on by pump().
    setEmergencySink([](const char *intent) {
      if (intent &&
          std::strcmp(intent, InteractiveDocument::kEmergencyQuitIntent) == 0) {
        const services::menu::Host &seam = services::menu::host();
        if (seam.quitNow)
          seam.quitNow();
      }
    });
  }

  // Show/hide report transitions only: a repeated sync must not re-dispatch
  // page callbacks (InteractiveDocument::show/hide are idempotent for the
  // same reason).
  void showDocument() {
    auto keepAlive = shared_from_this();
    if (isVisible())
      return;
    InteractiveDocument::show();
    module_resources::invoke(Lifetime.owner(), "document show", Desc.onShow,
                             Desc.userdata);
  }

  void hideDocument() {
    auto keepAlive = shared_from_this();
    if (!isVisible())
      return;
    // Before the DOM hides: a page closing its own composition/popups needs
    // the still-visible document.
    module_resources::invoke(Lifetime.owner(), "document hide", Desc.onHide,
                             Desc.userdata);
    InteractiveDocument::hide();
  }

  void closeDocument() {
    if (!isLoaded() || Closing)
      return;
    auto keepAlive = shared_from_this();
    Closing = true;
    module_resources::invoke(Lifetime.owner(), "document close", Desc.onClose,
                             Desc.userdata);
    close();
    Closing = false;
  }

  bool menuLayer() const {
    return (Desc.flags & ABCUI_DOC_FLAG_MENU) != 0;
  }

protected:
  void onDocumentReady() override {
    // Never fires for the built-in emergency page (degraded() reports it):
    // the page's ids do not exist there.
    module_resources::invoke(Lifetime.owner(), "document ready", Desc.onReady,
                             Desc.userdata);
  }

public:
  module_resources::Registration Lifetime;

private:
  bool Closing = false;
  AbcUiDocumentDesc Desc;
};

std::vector<std::shared_ptr<ModuleDocument>> &documents() {
  static std::vector<std::shared_ptr<ModuleDocument>> instance;
  return instance;
}

Rml::Context *&liveContext() {
  static Rml::Context *instance = nullptr;
  return instance;
}

ModuleDocument *resolve(AbcUiDocument *handle) {
  if (!handle)
    return nullptr;
  const auto *wanted = reinterpret_cast<ModuleDocument *>(handle);
  for (const auto &doc : documents()) {
    if (doc.get() == wanted)
      return doc.get();
  }
  return nullptr;
}

// One load attempt; the outcome is logged by InteractiveDocument itself
// (missing asset -> emergency page, Rml broken -> failed).
void loadDocument(ModuleDocument &doc) {
  if (Rml::Context *context = liveContext())
    doc.load(context);
}

} // namespace

AbcUiDocument *create(const AbcUiDocumentDesc &desc) {
  if (!module_resources::accepting())
    return nullptr;
  if (!desc.name || !*desc.name) {
    abc::ConsoleWarn("[AbcUi] module document without a name refused\n");
    return nullptr;
  }
  if (!desc.path || !*desc.path) {
    abc::ConsoleWarn("[AbcUi] module document \"%s\" without a path refused\n",
                     desc.name);
    return nullptr;
  }
  for (const auto &existing : documents()) {
    if (existing->getName() == desc.name) {
      abc::ConsoleWarn("[AbcUi] module document \"%s\" registered twice, "
                       "refused\n",
                       desc.name);
      return nullptr;
    }
  }
  auto doc = std::make_shared<ModuleDocument>(desc);
  // The raw pointer stays valid across a re-entrant create from onReady: the
  // vector may reallocate, the heap object does not.
  ModuleDocument *raw = doc.get();
  documents().push_back(doc);
  auto *handle = reinterpret_cast<AbcUiDocument *>(raw);
  raw->Lifetime.track([handle] { destroy(handle); });
  loadDocument(*raw);
  return resolve(handle) ? handle : nullptr;
}

void destroy(AbcUiDocument *handle) {
  ModuleDocument *target = resolve(handle);
  if (!target)
    return;
  for (auto it = documents().begin(); it != documents().end(); ++it) {
    if (it->get() == target) {
      auto closing = *it;
      documents().erase(it); // forget before onClose can re-enter destroy
      closing->closeDocument();
      return;
    }
  }
}

bool isDocument(AbcUiDocument *doc) {
  return resolve(doc) != nullptr;
}

Rml::ElementDocument *documentOf(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document ? document->document() : nullptr;
}

int32_t visible(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document && document->isVisible() ? 1 : 0;
}

int32_t degraded(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  return document && document->isDegraded() ? 1 : 0;
}

void show(AbcUiDocument *doc) {
  if (ModuleDocument *document = resolve(doc))
    document->showDocument();
}

void hide(AbcUiDocument *doc) {
  if (ModuleDocument *document = resolve(doc))
    document->hideDocument();
}

int32_t retry(AbcUiDocument *doc) {
  ModuleDocument *document = resolve(doc);
  if (!document)
    return 0;
  if (Rml::Context *context = liveContext()) {
    const InteractiveDocument::LoadOutcome outcome = document->retry(context);
    if (outcome == InteractiveDocument::LoadOutcome::kFailed)
      return 0;
  }
  return document->isLoaded() ? 1 : 0;
}

void contextReady(Rml::Context *context) {
  liveContext() = context;
  module_resources::contextRegistrationsOpen() = context != nullptr;
  if (!context)
    return;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    if (resolve(reinterpret_cast<AbcUiDocument *>(doc.get())))
      doc->load(context);
}

void pump() {
  Rml::Context *context = liveContext();
  if (!context)
    return;
  if (std::none_of(documents().begin(), documents().end(),
                   [](const auto &doc) { return doc->retryRequested(); }))
    return;
  const auto snapshot = documents();
  for (const auto &doc : snapshot) {
    if (!resolve(reinterpret_cast<AbcUiDocument *>(doc.get())) ||
        !doc->retryRequested())
      continue;
    doc->retry(context);
  }
}

void shutdown() {
  // onClose may register another document. Keep new descriptors unloaded
  // until the next context, rather than retaining a pointer into this one.
  liveContext() = nullptr;
  module_resources::contextRegistrationsOpen() = false;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    doc->closeDocument();
}

void unload() {
  liveContext() = nullptr;
  module_resources::contextRegistrationsOpen() = false;
  const auto snapshot = documents();
  for (const auto &doc : snapshot)
    doc->closeDocument();
  documents().clear();
}

void hideMenu() {
  const auto snapshot = documents();
  for (const auto &doc : snapshot) {
    if (resolve(reinterpret_cast<AbcUiDocument *>(doc.get())) &&
        doc->menuLayer() && doc->isVisible())
      doc->hideDocument();
  }
}

bool ownsVisible(Rml::ElementDocument *document) {
  if (!document)
    return false;
  for (const auto &doc : documents()) {
    if (doc->document() == document && doc->isVisible())
      return true;
  }
  return false;
}

} // namespace module_documents
} // namespace framework
