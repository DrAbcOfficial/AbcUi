// abc_ui_interactive - T05 acceptance for the interactive-document owner.
// Links the real InteractiveDocument.cpp against static RmlUi with a no-op
// renderer. ctest runs this target with the repo assets directory as the
// working directory. Cases: the explicit load-failure outcome with emergency
// fallback, listener lifecycle across close/reload (no stale binding onto
// recycled elements), repeated show/hide idempotence with focus, and the
// injected action sink. Keyboard input arbitration is out of scope (T06);
// text characters are covered by abc_ui_text_input.
#include "framework/InteractiveDocument.h"
#include "framework/DocumentLoadGuard.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

// cl_enginefunc_t + the gEngfuncs extern, with the SDK's include-order
// ritual already handled (same include set as abc_ui_hook_contract).
#include <metahook.h>

#include <windows.h>

#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "framework/TestHarness.h"
#include "framework/RmlTestHarness.h"
using abctest::NullRenderInterface;

// Localization.cpp stub: the VGUI2 language import is not linked here; the
// english table simply never loads and #tokens pass through unresolved.
namespace vgui2ext {
const char *currentLanguage() {
  return "english";
}
} // namespace vgui2ext

namespace {

// Silence the no-font / layout chatter; only errors surface (and count).
// T21: mirrors the production AbcSystemInterface::LogMessage - every Rml log
// message feeds the document-load guard, so the broken-RML case below
// exercises the real detector.
// T21: mirrors the production AbcSystemInterface::LogMessage - every Rml log
// message feeds the document-load guard, so the broken-RML case below
// exercises the real detector.
class QuietSystemInterface final : public abctest::QuietSystemInterface {
public:
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    framework::documentload::noteRmlLog(static_cast<int>(type),
                                        message.c_str());
    return abctest::QuietSystemInterface::LogMessage(type, message);
  }
};

// Test subclass carrying its own counting listener: proves the owner's
// detach-before-close with a listener object that outlives the document.
std::string emergencyFixturePath();

class CountingDocument final : public framework::InteractiveDocument {
public:
  explicit CountingDocument(const char *path)
      : InteractiveDocument("counting", path) {}
  ~CountingDocument() override {
    close();
  }

  class ClickCounter final : public Rml::EventListener {
  public:
    int Clicks = 0;
    void ProcessEvent(Rml::Event &) override {
      ++Clicks;
    }
  };

  ClickCounter Counter;
  int ReadyCount = 0;

  void watch(Rml::Element *target, const char *event) {
    addListener(target, event, &Counter);
  }

protected:
  void onDocumentReady() override {
    ++ReadyCount;
    if (Rml::Element *probe = document()->GetElementById("probe"))
      addListener(probe, "click", &Counter);
  }

  const char *recoveryPath() override {
    static const std::string path = emergencyFixturePath();
    return path.c_str();
  }
};

// Temp fixture independent of the repo assets (the missing-file case needs
// a path that is guaranteed absent).
std::string tempFixturePath(bool create) {
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  std::string path = std::string(dir) + "abc_ui_interactive_fixture.rml";
  if (create) {
    FILE *file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") == 0 && file) {
      fputs("<rml><head><style>div { display: block; }</style></head>"
            "<body id=\"counting\"><div id=\"probe\">probe</div></body></rml>",
            file);
      fclose(file);
    }
  } else {
    DeleteFileA(path.c_str());
  }
  return path;
}

bool focusBelongsTo(Rml::Context *context, Rml::ElementDocument *doc) {
  Rml::Element *focus = context->GetFocusElement();
  return focus && doc && focus->GetOwnerDocument() == doc;
}

// T21: a syntactically broken fixture (unclosed tags). RmlUi's lenient parser
// returns a partially parsed document; the load guard must still classify
// the load as failed.
std::string brokenFixturePath(bool create) {
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  std::string path = std::string(dir) + "abc_ui_interactive_broken.rml";
  if (create) {
    FILE *file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") == 0 && file) {
      fputs("<rml><head><style>body { display: block; }</style></head>"
            "<body id=\"counting\"><div id=\"probe\">probe",
            file);
      fclose(file);
    }
  } else {
    DeleteFileA(path.c_str());
  }
  return path;
}

// The recovery document the production framework loads when the main page is
// missing or broken. It carries the ids the cases below inspect.
std::string emergencyFixturePath() {
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  const std::string path =
      std::string(dir) + "abc_ui_interactive_emergency.rml";
  FILE *file = nullptr;
  if (fopen_s(&file, path.c_str(), "wb") == 0 && file) {
    fputs("<rml><head><style>div { display: block; }</style></head>"
          "<body id=\"counting\"><div id=\"emergency-retry\">Retry</div>"
          "<div id=\"emergency-quit\">Quit</div></body></rml>",
          file);
    fclose(file);
  }
  return path;
}

} // namespace

int main() {
  QuietSystemInterface systemInterface;
  NullRenderInterface renderer;
  Rml::SetSystemInterface(&systemInterface);
  Rml::SetRenderInterface(&renderer);
  if (!Rml::Initialise()) {
    std::cerr << "FAIL: Rml::Initialise\n";
    return 1;
  }
  // Layout stability for text-heavy documents; absence is tolerated (the
  // tests assert structure, not glyph metrics).
  CHECK(abctest::loadDefaultFont());
  Rml::Context *context = Rml::CreateContext("test", Rml::Vector2i(1280, 720));
  if (!context) {
    std::cerr << "FAIL: CreateContext\n";
    Rml::Shutdown();
    return 1;
  }

  {
    TEST_CASE("load-failure-emergency");
    CountingDocument missing(tempFixturePath(false).c_str());
    const auto outcome = missing.load(context);
    CHECK(outcome ==
          framework::InteractiveDocument::LoadOutcome::kEmergencyFallback);
    CHECK(missing.isDegraded());
    CHECK(missing.isLoaded());
    CHECK(missing.document() == context->GetDocument("counting"));
    CHECK(missing.ReadyCount == 0); // no page wiring against emergency DOM
    // The emergency page shows and focuses like any interactive document.
    missing.show();
    CHECK(missing.isVisible());
    CHECK(focusBelongsTo(context, missing.document()));
    missing.close();
    CHECK(!missing.isLoaded() && !missing.isDegraded());
  }

  {
    // T21: retry() recovering a real document once the file appears.
    TEST_CASE("emergency-retry-recovers");
    const std::string path = tempFixturePath(true);
    DeleteFileA(path.c_str());
    CountingDocument doc(path.c_str());
    CHECK(doc.load(context) ==
          framework::InteractiveDocument::LoadOutcome::kEmergencyFallback);
    // Recreate the fixture: the retry reloads the real path.
    tempFixturePath(true);
    CHECK(doc.retry(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    CHECK(!doc.isDegraded());
    CHECK(doc.ReadyCount == 1); // real page wiring ran
    doc.show();
    if (Rml::Element *probe = doc.document()->GetElementById("probe"))
      probe->DispatchEvent("click", Rml::Dictionary());
    CHECK(doc.Counter.Clicks == 1);
    // A healthy document: retry is a no-op returning kLoaded.
    CHECK(doc.retry(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    CHECK(doc.ReadyCount == 1);
    doc.close();
  }

  {
    // T21: a syntactically broken document. RmlUi returns a partially
    // parsed, non-null document plus "XML parse error" warnings - without
    // the load guard this would present an empty black page (the
    // live-caught fault). The guard redirects it into the emergency page:
    // degraded state, recovery buttons, no partial DOM left in the context.
    TEST_CASE("broken-document-emergency");
    CountingDocument doc(brokenFixturePath(true).c_str());
    const auto outcome = doc.load(context);
    CHECK(outcome ==
          framework::InteractiveDocument::LoadOutcome::kEmergencyFallback);
    CHECK(doc.isDegraded());
    CHECK(doc.ReadyCount == 0); // no page wiring against the discarded DOM
    // The partial parse must not linger in the context: exactly one
    // document carries the fixture id, and it is the emergency page.
    CHECK(doc.document() == context->GetDocument("counting"));
    CHECK(doc.document()->GetElementById("emergency-retry") != nullptr);
    // Recovering once the file is fixed: retry reloads the real path.
    brokenFixturePath(false);
    tempFixturePath(true);
    // NOTE: the fixture ids differ ("counting" is the name the class
    // passes) - rebuild the file at the SAME path the document uses, so
    // overwrite the broken file with valid markup instead.
    FILE *file = nullptr;
    std::string path = brokenFixturePath(true);
    CHECK(fopen_s(&file, path.c_str(), "wb") == 0 && file);
    fputs("<rml><head><style>div { display: block; }</style></head>"
          "<body id=\"counting\"><div id=\"probe\">probe</div></body></rml>",
          file);
    fclose(file);
    CHECK(doc.retry(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    CHECK(!doc.isDegraded());
    CHECK(doc.ReadyCount == 1);
    doc.close();
    brokenFixturePath(false);
  }

  {
    TEST_CASE("listener-lifecycle");
    CountingDocument doc(tempFixturePath(true).c_str());
    CHECK(doc.load(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    CHECK(!doc.isDegraded());
    CHECK(doc.ReadyCount == 1);
    doc.show();
    Rml::Element *probe = doc.document()->GetElementById("probe");
    CHECK(probe != nullptr);
    if (probe)
      probe->DispatchEvent("click", Rml::Dictionary());
    CHECK(doc.Counter.Clicks == 1);

    // Close, then reload into a fresh object. Rml recycles element
    // memory, so the old binding - had close() not detached it - could
    // resurface on the new DOM; the old counter must stay silent while
    // the fresh wiring fires.
    const int oldClicks = doc.Counter.Clicks;
    doc.close();
    CHECK(!doc.isLoaded());
    {
      CountingDocument reloaded(tempFixturePath(true).c_str());
      CHECK(reloaded.load(context) ==
            framework::InteractiveDocument::LoadOutcome::kLoaded);
      Rml::Element *probe2 = reloaded.document()->GetElementById("probe");
      CHECK(probe2 != nullptr);
      reloaded.show();
      if (probe2)
        probe2->DispatchEvent("click", Rml::Dictionary());
      CHECK(doc.Counter.Clicks == oldClicks); // stale binding silent
      CHECK(reloaded.Counter.Clicks == 1);    // fresh binding live
    }
  }

  {
    TEST_CASE("show-hide-idempotence");
    CountingDocument doc(tempFixturePath(true).c_str());
    CHECK(doc.load(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    doc.show();
    CHECK(doc.isVisible());
    CHECK(focusBelongsTo(context, doc.document()));
    Rml::Element *firstFocus = context->GetFocusElement();
    doc.show(); // repeated show: no re-focus, no churn
    CHECK(doc.isVisible());
    CHECK(context->GetFocusElement() == firstFocus);
    doc.hide();
    CHECK(!doc.isVisible());
    CHECK(context->GetFocusElement() == doc.document() ||
          !focusBelongsTo(context, doc.document()));
    doc.hide(); // repeated hide stays benign
    CHECK(!doc.isVisible());
    doc.show();
    CHECK(doc.isVisible() && focusBelongsTo(context, doc.document()));
    // Bindings survive hide/show cycles.
    if (Rml::Element *probe = doc.document()->GetElementById("probe"))
      probe->DispatchEvent("click", Rml::Dictionary());
    CHECK(doc.Counter.Clicks == 1);
    doc.close();
  }

  {
    TEST_CASE("fade-preserves-opacity-and-stops-input");
    abctest::TempDir fixture("abcui-dissolve");
    const std::string path = fixture.write(
        "page.rml",
        "<rml><head><style>"
        "body { width: 800px; height: 600px; opacity: 0.8; }"
        "#probe { position: absolute; left: 10px; top: 10px;"
        "width: 100px; height: 50px; opacity: 0.55;"
        "pointer-events: auto; focus: auto; }"
        "#nested { opacity: 0.2; }"
        "</style></head><body><button id='probe' autofocus='autofocus'>"
        "<span id='inherited'>text</span><span id='nested'>dim</span>"
        "</button><input id='editor' type='text' value='before'/>"
        "<div id='inline' style='opacity: 0.3;'>inline</div>"
        "</body></rml>");
    CountingDocument doc(path.c_str());
    CHECK(doc.load(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    doc.show();
    doc.pump(framework::InteractiveDocument::kFadeInSeconds);
    context->Update();
    Rml::Element *probe = doc.document()->GetElementById("probe");
    Rml::Element *inherited = doc.document()->GetElementById("inherited");
    Rml::Element *nested = doc.document()->GetElementById("nested");
    Rml::Element *inlineElement = doc.document()->GetElementById("inline");
    auto *editor = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(
        doc.document()->GetElementById("editor"));
    CHECK(editor != nullptr);
    CHECK_NEAR(probe->GetComputedValues().opacity(), 0.55, 0.001);
    CHECK_NEAR(doc.document()->GetComputedValues().opacity(), 0.8, 0.001);
    CHECK(editor->Focus());
    doc.watch(probe, "keydown");
    doc.watch(probe, "textinput");
    doc.watch(doc.document(), "keydown");
    const int clicks = doc.Counter.Clicks;
    doc.hide();
    CHECK(!doc.isVisible());
    CHECK(context->GetFocusElement() != probe);
    CHECK(context->GetFocusElement() != editor);
    probe->DispatchEvent("click", {});
    probe->DispatchEvent("keydown", {});
    Rml::Dictionary textInput;
    textInput["text"] = Rml::String("x");
    probe->DispatchEvent("textinput", textInput);
    context->ProcessTextInput("x");
    context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
    context->ProcessMouseMove(20, 20, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    CHECK(editor->GetValue() == "before");
    CHECK(doc.Counter.Clicks == clicks);
    Rml::Element *hit = context->GetElementAtPoint({20, 20});
    CHECK(!hit || hit->GetOwnerDocument() != doc.document());
    CHECK(!probe->Focus()); // even a locally focus:auto control is blocked
    doc.pump(framework::InteractiveDocument::kFadeOutSeconds * 0.5);
    context->Update();
    // Quadratic fade-out is at 25%, applied once per local opacity.
    CHECK_NEAR(doc.document()->GetComputedValues().opacity(), 0.2, 0.001);
    CHECK_NEAR(probe->GetComputedValues().opacity(), 0.1375, 0.001);
    CHECK_NEAR(inherited->GetComputedValues().opacity(), 0.1375, 0.001);
    CHECK_NEAR(nested->GetComputedValues().opacity(), 0.05, 0.001);
    CHECK_NEAR(inlineElement->GetComputedValues().opacity(), 0.075, 0.001);
    for (double delta : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -1.0})
      doc.pump(delta);
    CHECK(doc.document()->IsVisible());
    // Reverse without losing authored inline values or input policies.
    doc.show();
    doc.pump(framework::InteractiveDocument::kFadeInSeconds);
    context->Update();
    CHECK_NEAR(inlineElement->GetProperty<float>("opacity"), 0.3, 0.001);
    CHECK(probe->GetLocalStyleProperties().count(Rml::PropertyId::Opacity) ==
          0);
    CHECK(probe->GetComputedValues().pointer_events() ==
          Rml::Style::PointerEvents::Auto);
    CHECK(probe->Focus());
    probe->DispatchEvent("click", {});
    CHECK(doc.Counter.Clicks == clicks + 1);
    // A newer page-owned inline value survives the temporary fade overrides.
    context->ProcessMouseMove(20, 20, 0);
    context->ProcessMouseButtonDown(0, 0);
    const int beforeHide = doc.Counter.Clicks;
    doc.hide();
    context->ProcessMouseButtonUp(0, 0);
    CHECK(doc.Counter.Clicks == beforeHide);
    inlineElement->SetProperty("opacity", "0.4");
    doc.pump(0.01);
    doc.show();
    doc.pump(framework::InteractiveDocument::kFadeInSeconds);
    CHECK_NEAR(inlineElement->GetProperty<float>("opacity"), 0.4, 0.001);
    // Destroying a faded node cannot leave a dangling snapshot.
    doc.hide();
    auto late = doc.document()->CreateElement("button");
    late->SetProperty("pointer-events", "auto");
    late->SetProperty("focus", "auto");
    Rml::Element *lateControl = doc.document()->AppendChild(std::move(late));
    doc.pump(0.0);
    context->Update();
    CHECK(lateControl->GetComputedValues().pointer_events() ==
          Rml::Style::PointerEvents::None);
    CHECK(!lateControl->Focus());
    probe->GetParentNode()->RemoveChild(probe);
    doc.pump(framework::InteractiveDocument::kFadeOutSeconds);
    doc.close();
  }

  {
    TEST_CASE("show-hide-dissolve");
    CountingDocument doc(tempFixturePath(true).c_str());
    CHECK(doc.load(context) ==
          framework::InteractiveDocument::LoadOutcome::kLoaded);
    // A loaded page waits transparent, so its first show ramps in instead of
    // popping at full opacity.
    const Rml::Property *loaded = doc.document()->GetLocalProperty("opacity");
    CHECK(loaded != nullptr);
    if (loaded)
      CHECK_NEAR(loaded->Get<float>(), 0.0, 0.001);

    doc.show();
    CHECK(doc.isVisible());
    doc.pump(framework::InteractiveDocument::kFadeInSeconds * 0.5);
    const Rml::Property *midIn = doc.document()->GetProperty("opacity");
    CHECK(midIn != nullptr);
    if (midIn) {
      const float value = midIn->Get<float>();
      CHECK(value > 0.0f && value < 1.0f);
    }
    doc.pump(framework::InteractiveDocument::kFadeInSeconds);
    // A settled page carries no inline opacity: the stylesheet rules again.
    CHECK(doc.document()->GetLocalProperty("opacity") == nullptr);
    CHECK(doc.document()->IsVisible());

    // hide() flips the logical truth at once (the page logic and the menu
    // layer's hide-all rule never wait for an animation) while the document
    // keeps drawing until the ramp reaches zero.
    doc.hide();
    CHECK(!doc.isVisible());
    CHECK(doc.document()->IsVisible());
    doc.pump(framework::InteractiveDocument::kFadeOutSeconds * 0.5);
    CHECK(doc.document()->IsVisible());
    doc.pump(framework::InteractiveDocument::kFadeOutSeconds);
    CHECK(!doc.document()->IsVisible());

    // A show during the fade-out reverses it: no hide, no focus churn.
    doc.show();
    CHECK(doc.isVisible() && doc.document()->IsVisible());
    doc.pump(framework::InteractiveDocument::kFadeInSeconds * 0.5);
    doc.hide();
    CHECK(!doc.isVisible());
    doc.show();
    doc.pump(framework::InteractiveDocument::kFadeOutSeconds);
    CHECK(doc.isVisible() && doc.document()->IsVisible());

    // close() drops a fade in flight with the DOM.
    doc.hide();
    doc.close();
    CHECK(!doc.isVisible());
    CHECK(!doc.isLoaded());
  }

  tempFixturePath(false); // remove the fixture file
  Rml::RemoveContext("test");
  Rml::Shutdown();

  return abctest::finish("abc_ui_interactive");
}
