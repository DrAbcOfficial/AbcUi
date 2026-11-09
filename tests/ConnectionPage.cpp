// abc_ui_connection_page - the abcui.gameui connection surfaces driven through
// recording ABI seams (issue #133). The four documents are registered in the
// module's onLoad, which runs before any Rml context exists: a load-time
// documents->root() is null, so the click listeners MUST bind in the
// descriptor's onReady. Pins:
//   - no click listener exists until the document reports ready (the
//     load-time binding this regression came from could never have worked),
//   - each surface's button reaches its typed connection/menu intent once
//     ready (password connect submits the typed value, password cancel and
//     loading cancel are distinct intents, retry/return, direct connect),
//   - the delegated data-action walk (a child without the attribute owns none),
//   - onClose drops the binding and a fresh onReady restores it (a context
//     cycle reloads every document).
//
// The page module is linked as-is; only the framework table is a recording
// fake. No RmlUi, engine or game installation is involved.
#include <abcui/module_api.h>

#include "framework/TestHarness.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The page's shared table (the same inline Api the module source uses).
#include "shared/PageShared.h"

namespace connectionpage {
void load();
void shutdown();
void unload();
} // namespace connectionpage

namespace {

struct Recording {
  std::vector<std::string> passwords;
  std::string address;
  int cancels = 0;
  int passwordCancels = 0;
  int retries = 0;
  int returns = 0;
  int backs = 0;
  int subscribes = 0;
  int unsubscribes = 0;
};

Recording &recording() {
  static Recording value;
  return value;
}

struct FakeElement {
  std::string Value;
  std::map<std::string, std::string> Attributes;
  FakeElement *Parent = nullptr;
};

struct FakeDocument {
  AbcUiDocumentDesc Desc{};
  FakeElement Root;
  FakeElement RebuiltRoot;
  std::map<std::string, std::unique_ptr<FakeElement>> Elements;
  bool Loaded = false;
  bool Rebuilt = false;

  // A retry closes and re-creates the DOM, so the root identity changes.
  FakeElement *root() {
    return Rebuilt ? &RebuiltRoot : &Root;
  }
};

std::vector<std::unique_ptr<FakeDocument>> &documents() {
  static std::vector<std::unique_ptr<FakeDocument>> value;
  return value;
}

FakeDocument *documentOf(AbcUiDocument *document) {
  return reinterpret_cast<FakeDocument *>(document);
}

FakeElement *elementOf(AbcUiElement *element) {
  return reinterpret_cast<FakeElement *>(element);
}

FakeDocument *findDocument(const char *name) {
  for (const auto &document : documents()) {
    if (document->Desc.name && std::strcmp(document->Desc.name, name) == 0)
      return document.get();
  }
  return nullptr;
}

FakeElement *addElement(FakeDocument &document, const char *id) {
  auto element = std::make_unique<FakeElement>();
  element->Parent = &document.Root;
  FakeElement *raw = element.get();
  document.Elements[id] = std::move(element);
  return raw;
}

// The framework records the listener and hands back an id; removal is by id
// only (the real ui section behaves the same: the element is not required).
struct ListenerRecord {
  AbcUiElement *Element = nullptr;
  std::string Type;
  AbcUiElementListener Fn = nullptr;
  void *Userdata = nullptr;
};

std::map<uint64_t, ListenerRecord> &listeners() {
  static std::map<uint64_t, ListenerRecord> value;
  return value;
}

uint64_t nextListenerId = 1;

// --- documents ---------------------------------------------------------------

AbcUiDocument *apiCreate(const AbcUiDocumentDesc *desc) {
  if (!desc || !desc->name || !desc->path)
    return nullptr;
  auto document = std::make_unique<FakeDocument>();
  document->Desc = *desc;
  // The two text entries the intents read (the dialogs' fields).
  addElement(*document, "password-field");
  addElement(*document, "directconnect-field");
  FakeDocument *raw = document.get();
  documents().push_back(std::move(document));
  // A live context would load inside create(); the module boot order (the
  // one this test pins) registers before the context exists, so the page
  // has to wait for onReady.
  return reinterpret_cast<AbcUiDocument *>(raw);
}

AbcUiElement *apiRoot(AbcUiDocument *document) {
  FakeDocument *fake = documentOf(document);
  if (!fake || !fake->Loaded)
    return nullptr;
  return reinterpret_cast<AbcUiElement *>(fake->root());
}

// --- ui ----------------------------------------------------------------------

AbcUiElement *apiQuerySelector(AbcUiDocument *document, const char *id) {
  FakeDocument *fake = documentOf(document);
  if (!fake || !fake->Loaded || !id)
    return nullptr;
  const auto it = fake->Elements.find(id);
  return it == fake->Elements.end()
             ? nullptr
             : reinterpret_cast<AbcUiElement *>(it->second.get());
}

uint64_t apiAddListener(AbcUiElement *element, const char *type,
                        AbcUiElementListener listener, void *userdata) {
  if (!element || !type || !listener)
    return 0;
  const uint64_t id = nextListenerId++;
  listeners()[id] = ListenerRecord{element, type, listener, userdata};
  return id;
}

void apiRemoveListener(AbcUiElement *, const char *, uint64_t id) {
  listeners().erase(id);
}

AbcUiElement *apiParent(AbcUiElement *element) {
  FakeElement *fake = elementOf(element);
  return fake && fake->Parent ? reinterpret_cast<AbcUiElement *>(fake->Parent)
                              : nullptr;
}

int32_t copyOut(const std::string &text, char *out, uint32_t outSize) {
  const auto needed = static_cast<int32_t>(text.size());
  if (!out || outSize == 0)
    return needed;
  uint32_t count = static_cast<uint32_t>(needed);
  if (count >= outSize)
    count = outSize - 1;
  std::memcpy(out, text.data(), count);
  out[count] = '\0';
  return needed;
}

int32_t apiAttribute(AbcUiElement *element, const char *name, char *out,
                     uint32_t outSize) {
  FakeElement *fake = elementOf(element);
  if (!fake || !name)
    return copyOut(std::string(), out, outSize);
  const auto it = fake->Attributes.find(name);
  return copyOut(it == fake->Attributes.end() ? std::string() : it->second, out,
                 outSize);
}

int32_t apiElementValue(AbcUiElement *element, char *out, uint32_t outSize) {
  FakeElement *fake = elementOf(element);
  return copyOut(fake ? fake->Value : std::string(), out, outSize);
}

// --- events ------------------------------------------------------------------

AbcUiSubscription apiSubscribe(const char *, int, AbcUiEventSink, void *) {
  ++recording().subscribes;
  return static_cast<AbcUiSubscription>(nextListenerId++);
}

void apiUnsubscribe(AbcUiSubscription) {
  ++recording().unsubscribes;
}

// --- menu --------------------------------------------------------------------

void apiMenuState(AbcUiMenuState *out) {
  if (!out)
    return;
  AbcUiMenuState fresh{};
  fresh.structSize = sizeof(fresh);
  *out = fresh;
}

void apiMenuBack() {
  ++recording().backs;
}

// --- connection --------------------------------------------------------------

int32_t apiSubmitPassword(const char *password) {
  recording().passwords.push_back(password ? password : "");
  return 1;
}

void apiPasswordCancel() {
  ++recording().passwordCancels;
}

int32_t apiCancelLoading() {
  ++recording().cancels;
  return 1;
}

void apiRetryFailed() {
  ++recording().retries;
}

void apiReturnHome() {
  ++recording().returns;
}

int32_t apiConnectAddress(const char *address, char *, uint32_t) {
  recording().address = address ? address : "";
  return 1; // accepted: no inline hint write
}

// --- the click helper --------------------------------------------------------

// Delivers one click to the document's root listener. The delegate flag models
// the nested-row shape: the clicked child carries no data-action and the
// owning ancestor does. Returns the listener's answer (0 when nothing is
// bound).
int32_t click(FakeDocument &document, const char *action, bool delegate) {
  if (!document.Loaded)
    return 0;
  FakeElement *root = document.root();
  FakeElement carrier;
  carrier.Attributes["data-action"] = action ? action : "";
  carrier.Parent = root;
  FakeElement target;
  target.Parent = delegate ? &carrier : root;
  if (!delegate)
    target.Attributes["data-action"] = action ? action : "";
  AbcUiElementEvent event{};
  event.structSize = sizeof(event);
  event.type = "click";
  for (const auto &entry : listeners()) {
    if (entry.second.Element != reinterpret_cast<AbcUiElement *>(root))
      continue;
    if (entry.second.Type != "click")
      continue;
    return entry.second.Fn(entry.second.Userdata,
                           reinterpret_cast<AbcUiElement *>(&target), &event);
  }
  return 0;
}

int32_t click(FakeDocument &document, const char *action) {
  return click(document, action, false);
}

size_t listenerCount(FakeDocument &document) {
  size_t count = 0;
  for (const auto &entry : listeners()) {
    if (entry.second.Element ==
            reinterpret_cast<AbcUiElement *>(document.root()) &&
        entry.second.Type == "click")
      ++count;
  }
  return count;
}

void ready(FakeDocument &document) {
  document.Loaded = true;
  if (document.Desc.onReady)
    document.Desc.onReady(document.Desc.userdata);
}

void close(FakeDocument &document) {
  if (document.Desc.onClose)
    document.Desc.onClose(document.Desc.userdata);
  document.Loaded = false;
}

} // namespace

int main() {
  auto &r = recording();

  AbcUiDocumentsApi documentsApi{};
  documentsApi.structSize = sizeof(documentsApi);
  documentsApi.create = &apiCreate;
  documentsApi.root = &apiRoot;

  AbcUiUiApi uiApi{};
  uiApi.structSize = sizeof(uiApi);
  uiApi.querySelector = &apiQuerySelector;
  uiApi.elementAddListener = &apiAddListener;
  uiApi.elementRemoveListener = &apiRemoveListener;
  uiApi.elementParent = &apiParent;
  uiApi.elementAttribute = &apiAttribute;
  uiApi.elementValue = &apiElementValue;

  AbcUiEventsApi eventsApi{};
  eventsApi.structSize = sizeof(eventsApi);
  eventsApi.subscribe = &apiSubscribe;
  eventsApi.unsubscribe = &apiUnsubscribe;

  AbcUiMenuApi menuApi{};
  menuApi.structSize = sizeof(menuApi);
  menuApi.state = &apiMenuState;
  menuApi.back = &apiMenuBack;

  AbcUiConnectionApi connectionApi{};
  connectionApi.structSize = sizeof(connectionApi);
  connectionApi.submitPassword = &apiSubmitPassword;
  connectionApi.passwordCancel = &apiPasswordCancel;
  connectionApi.cancelLoading = &apiCancelLoading;
  connectionApi.retryFailed = &apiRetryFailed;
  connectionApi.returnHome = &apiReturnHome;
  connectionApi.connectAddress = &apiConnectAddress;

  AbcUiModuleApi table{};
  table.structSize = sizeof(table);
  table.events = &eventsApi;
  table.documents = &documentsApi;
  table.ui = &uiApi;
  table.menu = &menuApi;
  table.connection = &connectionApi;

  Api = &table;
  connectionpage::load();

  TEST_CASE("documents register before the context exists");
  CHECK(documents().size() == 4);
  CHECK(r.subscribes == 2);
  for (const char *name :
       {"menuloading", "menuconnerror", "menupassword", "menudirectconnect"}) {
    FakeDocument *document = findDocument(name);
    CHECK(document != nullptr);
    if (!document)
      continue;
    CHECK(document->Desc.onReady != nullptr);
    CHECK(document->Desc.onClose != nullptr);
    // The load-time documents->root() is null: nothing could be bound here.
    CHECK(apiRoot(reinterpret_cast<AbcUiDocument *>(document)) == nullptr);
    CHECK(listenerCount(*document) == 0);
  }

  TEST_CASE("onReady binds exactly one click listener per surface");
  for (const auto &document : documents())
    ready(*document);
  for (const auto &document : documents())
    CHECK(listenerCount(*document) == 1);
  // A repeated ready before close must not stack a second listener.
  ready(*findDocument("menupassword"));
  CHECK(listenerCount(*findDocument("menupassword")) == 1);

  TEST_CASE("password connect submits the typed value");
  FakeDocument *password = findDocument("menupassword");
  CHECK(password != nullptr);
  if (password) {
    const auto field = password->Elements.find("password-field");
    CHECK(field != password->Elements.end());
    if (field != password->Elements.end())
      field->second->Value = "hunter2";
    CHECK(click(*password, "password-connect") == 1);
    CHECK(r.passwords.size() == 1);
    if (!r.passwords.empty())
      CHECK(r.passwords.back() == "hunter2");
  }

  TEST_CASE("password cancel is its own intent");
  if (password) {
    CHECK(click(*password, "password-cancel") == 1);
    CHECK(r.passwordCancels == 1);
  }

  TEST_CASE("a delegated text child resolves the owning action");
  if (password) {
    CHECK(click(*password, "password-cancel", true) == 1);
    CHECK(r.passwordCancels == 2);
  }

  TEST_CASE("an unknown action consumes nothing");
  if (password) {
    CHECK(click(*password, "password-nope") == 0);
    CHECK(r.passwords.size() == 1);
    CHECK(r.passwordCancels == 2);
  }

  TEST_CASE("loading cancel reaches the model latch");
  FakeDocument *loading = findDocument("menuloading");
  CHECK(loading != nullptr);
  if (loading) {
    CHECK(click(*loading, "loading-cancel") == 1);
    CHECK(r.cancels == 1);
  }

  TEST_CASE("error retry and return are distinct intents");
  FakeDocument *error = findDocument("menuconnerror");
  CHECK(error != nullptr);
  if (error) {
    CHECK(click(*error, "error-retry") == 1);
    CHECK(r.retries == 1);
    CHECK(click(*error, "error-return") == 1);
    CHECK(r.returns == 1);
  }

  TEST_CASE("direct connect submits the typed address and cancels back");
  FakeDocument *direct = findDocument("menudirectconnect");
  CHECK(direct != nullptr);
  if (direct) {
    const auto field = direct->Elements.find("directconnect-field");
    CHECK(field != direct->Elements.end());
    if (field != direct->Elements.end())
      field->second->Value = "example.com:27015";
    CHECK(click(*direct, "directconnect-connect") == 1);
    CHECK(r.address == "example.com:27015");
    CHECK(click(*direct, "directconnect-cancel") == 1);
    CHECK(r.backs == 1);
  }

  TEST_CASE("onClose drops the binding and a fresh onReady restores it");
  if (password) {
    const int before = r.passwordCancels;
    close(*password);
    CHECK(listenerCount(*password) == 0);
    CHECK(click(*password, "password-cancel") == 0);
    CHECK(r.passwordCancels == before);
    ready(*password);
    CHECK(listenerCount(*password) == 1);
    CHECK(click(*password, "password-cancel") == 1);
    CHECK(r.passwordCancels == before + 1);
  }

  TEST_CASE("a retried document rebinds on its new root");
  if (password) {
    // A retry replaces the DOM without the descriptor's onClose: the stale
    // id can never fire again, so the ready path must replace it.
    const int before = r.passwordCancels;
    password->Loaded = false;
    password->Rebuilt = true;
    ready(*password);
    CHECK(listenerCount(*password) == 1);
    CHECK(click(*password, "password-cancel") == 1);
    CHECK(r.passwordCancels == before + 1);
  }

  TEST_CASE("shutdown forgets every surface");
  connectionpage::shutdown();
  for (const auto &document : documents())
    CHECK(listenerCount(*document) == 0);

  TEST_CASE("unload releases the menu-state subscriptions");
  connectionpage::unload();
  CHECK(r.unsubscribes == 2);
  Api = nullptr;
  return abctest::finish("abc_ui_connection_page");
}
