// abc_ui_bar_animation - the shared bar ramp (modules/base/assets/rcss/
// barmotion.rcss) driven the way the HUD modules drive it: a width ladder
// class on the fill and a direction class on the box, with a controllable
// clock. Covers the drop/gain durations, the instant snap, the inert relayout
// when the fill's box changes size, and the idempotent rewrite a repeated
// readout produces.
#include "framework/RmlTestHarness.h"
#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

#include <RmlUi/Core.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

class ClockSystem final : public abctest::QuietSystemInterface {
public:
  double Now = 0.0;
  double GetElapsedTime() override {
    return Now;
  }
  bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
    if (type <= Rml::Log::LT_WARNING) {
      ++Messages;
      std::cerr << "rmlui(" << static_cast<int>(type) << "): " << message
                << "\n";
    }
    return abctest::QuietSystemInterface::LogMessage(type, message);
  }
  int Messages = 0;
};

// Icons and backgrounds are never inspected offline; accepting every texture
// keeps the render pass free of "could not load" warnings.
class TextureRenderInterface final : public abctest::NullRenderInterface {
public:
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                              Rml::Span<const int>) override {
    return 1;
  }
  Rml::TextureHandle LoadTexture(Rml::Vector2i &size,
                                 const Rml::String &) override {
    size = Rml::Vector2i(1, 1);
    return 1;
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                     Rml::Vector2i) override {
    return 1;
  }
  void ReleaseTexture(Rml::TextureHandle) override {}
};

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Advance the fake clock in frame-sized steps: RmlUi clamps one update's time
// step to 0.1s, so a large jump would not model real playback.
void advance(Rml::Context *context, ClockSystem &system, double seconds) {
  constexpr double kFrame = 0.02;
  for (double elapsed = 0.0; elapsed < seconds; elapsed += kFrame) {
    system.Now += kFrame;
    context->Update();
  }
}

void setClasses(Rml::Element *element, const std::string &classes) {
  element->SetAttribute("class", classes);
}

float widthOf(Rml::Element *element) {
  return element->GetOffsetWidth();
}

} // namespace

int main() {
  ClockSystem system;
  TextureRenderInterface renderer;
  Rml::SetSystemInterface(&system);
  Rml::SetRenderInterface(&renderer);
  if (!Rml::Initialise()) {
    std::cerr << "FAIL: Rml::Initialise\n";
    return 1;
  }
  if (!abctest::loadDefaultFont()) {
    std::cerr << "FAIL: test fonts\n";
    Rml::Shutdown();
    return 1;
  }

  const std::filesystem::path source = abctest::sourceDir();
  abctest::TempDir temp("abcui-bar-animation");
  const std::string sheet = temp.write(
      "barmotion.rcss",
      readFile(source / "modules/base/assets/rcss/barmotion.rcss").c_str());
  CHECK(!sheet.empty());

  // The fixture mirrors the modules' structure: a positioned box (the meter
  // well or one segmented cell) whose fill carries the width ladder class.
  const std::string markup =
      "<rml><head>"
      "<link type=\"text/rcss\" href=\"" +
      sheet +
      "\"/>"
      "<style>"
      "body { font-family: Lato; }"
      "#page { position: absolute; left: 0; top: 0; width: 200dp; "
      "height: 100dp; }"
      "#track { position: absolute; left: 0; top: 0; width: 100dp; "
      "height: 5dp; background-color: #202020; }"
      ".fill { position: absolute; left: 0; top: 0; height: 5dp; }"
      "#fill { position: absolute; left: 0; top: 0; height: 5dp; "
      "background-color: #ffb000; }"
      "</style></head><body id=\"page\">"
      "<div id=\"box\" class=\"bar-motion\"><div id=\"track\">"
      "<div id=\"fill\" class=\"fill bar-fill-100\"></div>"
      "</div></div></body></rml>";
  const std::string documentPath = temp.write("bar.rml", markup.c_str());

  Rml::Context *context = Rml::CreateContext("bar", Rml::Vector2i(800, 600));
  if (!context) {
    std::cerr << "FAIL: CreateContext\n";
    Rml::Shutdown();
    return 1;
  }
  Rml::ElementDocument *document = context->LoadDocument(documentPath);
  CHECK(document != nullptr);
  if (!document) {
    Rml::RemoveContext("bar");
    Rml::Shutdown();
    return 1;
  }
  document->Show();
  context->Update();
  context->Render();

  Rml::Element *box = document->GetElementById("box");
  Rml::Element *track = document->GetElementById("track");
  Rml::Element *fill = document->GetElementById("fill");
  CHECK(box != nullptr);
  CHECK(track != nullptr);
  CHECK(fill != nullptr);
  if (!box || !track || !fill) {
    Rml::RemoveContext("bar");
    Rml::Shutdown();
    return 1;
  }

  TEST_CASE("document-loads");
  CHECK(system.Messages == 0);
  CHECK(system.Errors == 0);
  CHECK_NEAR(widthOf(fill), 100.0, 0.5);

  TEST_CASE("drop-ramps");
  // 100 -> 60 with the drop class: the first update starts the ramp from the
  // old width, a frame later the fill is between the two, and after the
  // 0.12s duration it lands exactly on the ladder percentage.
  setClasses(box, "bar-motion bar-drop");
  setClasses(fill, "fill bar-fill-60");
  context->Update();
  CHECK(widthOf(fill) > 60.0f);
  advance(context, system, 0.04);
  CHECK(widthOf(fill) < 100.0f);
  CHECK(widthOf(fill) > 60.0f);
  advance(context, system, 0.30);
  CHECK_NEAR(widthOf(fill), 60.0, 0.5);

  TEST_CASE("gain-is-slower");
  // 60 -> 100 with the gain class: at the drop duration (0.12s) the fill is
  // still short of the target.
  setClasses(box, "bar-motion bar-gain");
  setClasses(fill, "fill bar-fill-100");
  context->Update();
  advance(context, system, 0.12);
  CHECK(widthOf(fill) < 100.0f);
  CHECK(widthOf(fill) > 60.0f);
  advance(context, system, 0.40);
  CHECK_NEAR(widthOf(fill), 100.0, 0.5);

  TEST_CASE("snap-is-instant");
  setClasses(box, "bar-motion bar-snap");
  setClasses(fill, "fill bar-fill-30");
  context->Update();
  CHECK_NEAR(widthOf(fill), 30.0, 0.5);
  advance(context, system, 0.05);
  CHECK_NEAR(widthOf(fill), 30.0, 0.5);

  TEST_CASE("relayout-is-inert");
  // The segmented bar changes its cells' dp width when the maximum changes;
  // the fill's percentage stays and the used width follows instantly.
  track->SetProperty("width", "40dp");
  context->Update();
  CHECK_NEAR(widthOf(fill), 12.0, 0.5);
  advance(context, system, 0.10);
  CHECK_NEAR(widthOf(fill), 12.0, 0.5);

  TEST_CASE("repeated-write-keeps-the-ramp");
  // A repeated readout rewrites the same classes; it must not restart (or
  // otherwise disturb) the ramp that is still in flight.
  setClasses(box, "bar-motion bar-drop");
  setClasses(fill, "fill bar-fill-10");
  context->Update();
  advance(context, system, 0.06);
  const float inFlight = widthOf(fill);
  CHECK(inFlight > 4.0f);
  CHECK(inFlight < 12.0f);
  setClasses(box, "bar-motion bar-drop");
  setClasses(fill, "fill bar-fill-10");
  context->Update();
  CHECK_NEAR(widthOf(fill), inFlight, 0.5);
  advance(context, system, 0.20);
  CHECK_NEAR(widthOf(fill), 4.0, 0.5);

  TEST_CASE("ladder-is-complete");
  // Every percent the helper can emit must have a ladder rule: the block is
  // generated, so this catches a missing or mistyped row.
  {
    Rml::ElementPtr ladder = document->CreateElement("div");
    Rml::Element *ladderTrack = ladder.get();
    if (ladderTrack) {
      ladderTrack->SetProperty("position", "absolute");
      ladderTrack->SetProperty("left", "0dp");
      ladderTrack->SetProperty("top", "20dp");
      ladderTrack->SetProperty("width", "100dp");
      ladderTrack->SetProperty("height", "60dp");
      std::string fills;
      for (int percent = 0; percent <= 100; ++percent)
        fills += "<div class=\"fill bar-fill-" + std::to_string(percent) +
                 "\"></div>";
      ladderTrack->SetInnerRML(fills);
      document->AppendChild(std::move(ladder));
      context->Update();
      for (int percent = 0; percent <= 100; ++percent) {
        Rml::Element *child = ladderTrack->GetChild(percent);
        CHECK_NEAR(child ? child->GetOffsetWidth() : -1.0f, percent, 0.2);
      }
    }
  }

  TEST_CASE("zero-warnings");
  CHECK(system.Messages == 0);
  CHECK(system.Errors == 0);

  document->Close();
  Rml::RemoveContext("bar");
  Rml::Shutdown();
  return abctest::finish("abc_ui_bar_animation");
}
