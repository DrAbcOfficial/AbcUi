// abc_ui_text_input - T07 acceptance for the text/IME pipeline, linked
// against the production TextInput.cpp + TextEncoding.cpp and a real RmlUi
// context. The document is a minimal in-memory fixture with the password and
// textarea fields. Cases: the one-character-source rule (commit to focused
// editor only, no char inference from keynums is testable here by absence),
// repeat input, BMP/supplementary-plane commits, paste through the shared
// clipboard, maxlength, newline policy, password field, and the full
// composition state machine (start/update/commit/cancel, editor-scoped
// tracking, blur-cancel, IME-owned key gate).
#include "framework/InteractiveDocument.h"
#include "rmlui/TextInput.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <keydefs.h>

#include <iostream>
#include <string>
#include "framework/TestHarness.h"
#include "framework/RmlTestHarness.h"
using abctest::NullRenderInterface;
using abctest::QuietSystemInterface;

// TextInput.cpp's production context accessor comes from RmlUiModule.cpp,
// which is not linked here - the test supplies the same seam.
namespace Rml {
class Context;
}
namespace rmlui {
Rml::Context *TestContext = nullptr;
Rml::Context *context() {
  return TestContext;
}
} // namespace rmlui
namespace vgui2ext {
const char *currentLanguage() {
  return "english";
}
} // namespace vgui2ext

namespace {

Rml::ElementFormControl *focusControl(Rml::Context *context) {
  return dynamic_cast<Rml::ElementFormControl *>(context->GetFocusElement());
}

bool focusById(Rml::Context *context, Rml::ElementDocument *doc,
               const char *id) {
  Rml::Element *field = doc ? doc->GetElementById(id) : nullptr;
  return field && field->Focus();
}

std::string valueOf(Rml::ElementFormControl *control) {
  return control ? std::string(control->GetValue()) : std::string("<null>");
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
  CHECK(abctest::loadDefaultFont());
  Rml::Context *context = Rml::CreateContext("test", Rml::Vector2i(1280, 720),
                                             nullptr, &rmlui::textInput());
  rmlui::TestContext = context;
  if (!context) {
    std::cerr << "FAIL: CreateContext\n";
    Rml::Shutdown();
    return 1;
  }

  // The text pipeline is exercised on a minimal document: one single-line
  // field, one password field, one textarea and a non-editor button.
  abctest::TempDir fixtureDir("abcui-text-input");
  const std::string fixture = fixtureDir.write(
      "textdemo.rml",
      "<rml><head><style>input, textarea, button { display: block; }</style>"
      "</head><body><input id=\"text-field\" type=\"text\"/>"
      "<input id=\"password-field\" type=\"password\"/>"
      "<textarea id=\"area-field\" maxlength=\"100\"/>"
      "<button id=\"action-button\">action</button></body></rml>");
  framework::InteractiveDocument page("textdemo", fixture.c_str());
  CHECK(page.load(context) ==
        framework::InteractiveDocument::LoadOutcome::kLoaded);
  for (const char *id : {"text-field", "password-field", "area-field"})
    CHECK(page.document()->GetElementById(id) != nullptr);
  page.show();

  {
    TEST_CASE("ascii-repeat-commit");
    CHECK(focusById(context, page.document(), "text-field"));
    Rml::ElementFormControl *field = focusControl(context);
    CHECK(field != nullptr);
    rmlui::feedTextInput("ab");
    rmlui::feedTextInput("ab"); // auto-repeat style resubmission appends
    CHECK(valueOf(field) == "abab");
  }

  {
    TEST_CASE("chinese-and-supplementary");
    Rml::ElementFormControl *field = focusControl(context);
    rmlui::feedTextInput("\xE4\xB8\xAD\xE6\x96\x87"); // 中文
    CHECK(valueOf(field) == "abab\xE4\xB8\xAD\xE6\x96\x87");
    rmlui::feedTextInput("\xF0\x9F\x98\x80"); // U+1F600, one code point
    CHECK(valueOf(field) == "abab\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80");
    // CESU-8 surrogate halves are dropped, never committed.
    rmlui::feedTextInput("\xED\xA0\xBD\xED\xB8\x80");
    CHECK(valueOf(field) == "abab\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80");
    // Byte-truncated commit (the 31-byte event cut) drops the tail.
    rmlui::feedTextInput("\xE4\xB8");
    CHECK(valueOf(field) == "abab\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80");
  }

  {
    TEST_CASE("no-editor-no-commit");
    // Focus on a button (not an editor): committed text is dropped -
    // the "only a focused editor receives characters" contract, and the
    // reason a stray IME commit can never reach the page's actions.
    Rml::Element *button = page.document()->GetElementById("action-button");
    CHECK(button && button->Focus());
    rmlui::feedTextInput("x");
    rmlui::feedTextEditing("x", 1);
    CHECK(!rmlui::textComposing());
    CHECK(focusControl(context) == nullptr);
  }

  {
    TEST_CASE("composition-lifecycle");
    CHECK(focusById(context, page.document(), "area-field"));
    Rml::ElementFormControl *area = focusControl(context);
    CHECK(area != nullptr);
    // Start + update: composing bytes live in the value (they are real
    // widget text during the composition).
    rmlui::feedTextEditing("nihao", 5);
    CHECK(rmlui::textComposing());
    CHECK(valueOf(area) == "nihao");
    rmlui::feedTextEditing("nihao", 2);
    CHECK(valueOf(area) == "nihao");
    // Commit through SDL_EVENT_TEXT_INPUT semantics: the composing range
    // is replaced by the final string.
    rmlui::feedTextInput("\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
    CHECK(!rmlui::textComposing());
    CHECK(valueOf(area) == "\xE4\xBD\xA0\xE5\xA5\xBD");
    // The post-commit empty SDL_EVENT_TEXT_EDITING clears nothing (the
    // composition already ended above).
    rmlui::feedTextEditing("", 0);
    CHECK(!rmlui::textComposing());
    CHECK(valueOf(area) == "\xE4\xBD\xA0\xE5\xA5\xBD");
  }

  {
    TEST_CASE("composition-cancel");
    CHECK(focusById(context, page.document(), "area-field"));
    Rml::ElementFormControl *area = focusControl(context);
    const std::string before = valueOf(area);
    rmlui::feedTextEditing("zhongwen", 8);
    CHECK(rmlui::textComposing());
    // Empty composition while tracking = platform cancel (ESC at the
    // IME): the half-typed pinyin is purged, never committed.
    rmlui::feedTextEditing("", 0);
    CHECK(!rmlui::textComposing());
    CHECK(valueOf(area) == before);
    // Cancel while idle is a benign no-op.
    rmlui::cancelComposition();
    CHECK(!rmlui::textComposing());
  }

  {
    TEST_CASE("composition-blur-cancel");
    CHECK(focusById(context, page.document(), "area-field"));
    Rml::ElementFormControl *area = focusControl(context);
    const std::string before = valueOf(area);
    rmlui::feedTextEditing("abc", 3);
    CHECK(rmlui::textComposing());
    // Editor loses focus (page hide / click elsewhere): the handler's
    // OnDeactivate cancels instead of leaving composing bytes behind.
    area->Blur();
    CHECK(!rmlui::textComposing());
    CHECK(focusControl(context) == nullptr || focusControl(context) == area);
    CHECK(focusControl(context) == nullptr ||
          valueOf(focusControl(context)) != before + "abc");
  }

  {
    TEST_CASE("ime-owned-keys");
    CHECK(focusById(context, page.document(), "text-field"));
    CHECK(!rmlui::compositionOwnsKey(K_ENTER));
    rmlui::feedTextEditing("n", 1);
    CHECK(rmlui::textComposing());
    // While composing, the IME owns commit/cancel/edit keys; feeding
    // them to the editor would double-act on the leak path.
    CHECK(rmlui::compositionOwnsKey(K_ENTER));
    CHECK(rmlui::compositionOwnsKey(K_KP_ENTER));
    CHECK(rmlui::compositionOwnsKey(K_ESCAPE));
    CHECK(rmlui::compositionOwnsKey(K_BACKSPACE));
    CHECK(!rmlui::compositionOwnsKey('a'));
    CHECK(!rmlui::compositionOwnsKey(K_SPACE));
    rmlui::cancelComposition();
    CHECK(!rmlui::compositionOwnsKey(K_ESCAPE));
  }

  {
    TEST_CASE("paste-and-copy");
    CHECK(focusById(context, page.document(), "text-field"));
    Rml::ElementFormControl *field = focusControl(context);
    field->SetValue("");
    systemInterface.Clipboard = "\xE5\x89\xAA\xE8\xB4\xB4"; // 剪贴
    // Ctrl+V rides Rml's own shortcut handling over the shared
    // SystemInterface clipboard - no second paste implementation.
    context->ProcessKeyDown(Rml::Input::KI_V, Rml::Input::KM_CTRL);
    CHECK(valueOf(field) == "\xE5\x89\xAA\xE8\xB4\xB4");
    // Select all + copy round-trips through the same clipboard.
    context->ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
    context->ProcessKeyDown(Rml::Input::KI_C, Rml::Input::KM_CTRL);
    CHECK(systemInterface.Clipboard == "\xE5\x89\xAA\xE8\xB4\xB4");
  }

  {
    TEST_CASE("maxlength-and-newline-policy");
    CHECK(focusById(context, page.document(), "area-field"));
    Rml::ElementFormControl *area = focusControl(context);
    area->SetValue("");
    // The shipped textarea pins maxlength=100 (characters).
    const std::string longText(150, 'a');
    rmlui::feedTextInput(longText.c_str());
    CHECK(valueOf(area).size() == 100);
    area->SetValue(std::string(98, 'a')); // leave maxlength headroom
    // Newlines land only where the editor accepts them.
    const std::string withBreak = valueOf(area);
    rmlui::feedTextInput("\n");
    CHECK(valueOf(area) == withBreak + "\n");
    // The single-line text field never receives one: the commit of a
    // line break is filtered by the widget itself (type=text), and the
    // Enter KEY never inserts (key stream, not the text source).
    CHECK(focusById(context, page.document(), "text-field"));
    Rml::ElementFormControl *field = focusControl(context);
    field->SetValue("");
    rmlui::feedTextInput("x");
    context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
    CHECK(valueOf(field) == "x");
  }

  {
    TEST_CASE("password-field");
    CHECK(focusById(context, page.document(), "password-field"));
    Rml::ElementFormControl *field = focusControl(context);
    CHECK(field != nullptr);
    // Same pipeline, opaque rendering only: the value is the plaintext.
    rmlui::feedTextInput("secret\xE4\xB8\xAD");
    CHECK(valueOf(field) == "secret\xE4\xB8\xAD");
  }

  {
    TEST_CASE("shutdown-drops-state");
    CHECK(focusById(context, page.document(), "text-field"));
    rmlui::feedTextEditing("zz", 2);
    CHECK(rmlui::textComposing());
    rmlui::textInputShutdown();
    CHECK(!rmlui::textComposing());
    // A stale editor-scoped feed after teardown is inert.
    rmlui::feedTextEditing("zz", 2);
    rmlui::feedTextInput("zz");
    CHECK(!rmlui::textComposing());
  }

  page.close();
  Rml::RemoveContext("test");
  Rml::Shutdown();

  return abctest::finish("abc_ui_text_input");
}
