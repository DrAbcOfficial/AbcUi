// abc_ui_health_document - the shipped abcui.health (classic vitals) document
// parsed by the real RmlUi: every id the module drives exists, the shipped
// RCSS and RML produce zero warnings, the meter fill lives inside its well so
// the bar-fill-N percentage resolves against the 92dp track, and the grouped
// hidden slots keep their boxes.
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

class CountingSystemInterface final : public abctest::QuietSystemInterface {
public:
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

// The document carries the additive icons; the null renderer returns empty
// texture handles, which RmlUi reports as a load failure. This renderer
// accepts every texture (the pixel data itself is never inspected offline).
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

void replaceAll(std::string &text, const std::string &from,
                const std::string &to) {
  std::size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) {
    text.replace(at, from.size(), to);
    at += to.size();
  }
}

bool hasId(Rml::ElementDocument *document, const char *id) {
  return document && document->GetElementById(id) != nullptr;
}

} // namespace

int main() {
  CountingSystemInterface systemInterface;
  TextureRenderInterface renderer;
  Rml::SetSystemInterface(&systemInterface);
  Rml::SetRenderInterface(&renderer);
  if (!Rml::Initialise()) {
    std::cerr << "FAIL: Rml::Initialise\n";
    return 1;
  }
  // The vitals numerals are bold; the bundled Lato regular face alone would
  // log "no font face defined" for every value box.
  const std::string boldFont =
      (abctest::sourceDir() /
       "thirdparty/RmlUi/Samples/assets/LatoLatin-Bold.ttf")
          .string();
  if (!abctest::loadDefaultFont() ||
      !Rml::LoadFontFace(boldFont, "Lato", Rml::Style::FontStyle::Normal,
                         Rml::Style::FontWeight::Bold)) {
    std::cerr << "FAIL: test fonts\n";
    Rml::Shutdown();
    return 1;
  }

  const std::filesystem::path source = abctest::sourceDir();
  const std::filesystem::path module = source / "modules/health/assets/hud";
  const std::filesystem::path baseRcss =
      source / "modules/base/assets/rcss/base.rcss";
  const std::filesystem::path motionRcss =
      source / "modules/base/assets/rcss/barmotion.rcss";
  const std::filesystem::path healthRcss = module / "rcss/health.rcss";
  const std::filesystem::path healthRml = module / "rml/health.rml";

  abctest::TempDir temp("abcui-health-document");
  std::string base = readFile(baseRcss);
  std::string motion = readFile(motionRcss);
  std::string sheet = readFile(healthRcss);
  CHECK(!base.empty());
  CHECK(!motion.empty());
  CHECK(!sheet.empty());
  replaceAll(base, "Verdana", "Lato");
  replaceAll(sheet, "Verdana", "Lato");
  const std::string basePath = temp.write("base.rcss", base.c_str());
  const std::string motionPath = temp.write("barmotion.rcss", motion.c_str());
  const std::string sheetPath = temp.write("health.rcss", sheet.c_str());

  std::string markup = readFile(healthRml);
  CHECK(!markup.empty());
  replaceAll(markup, "mod://abcui.base/rcss/base.rcss", basePath);
  replaceAll(markup, "mod://abcui.base/rcss/barmotion.rcss", motionPath);
  replaceAll(markup, "../rcss/health.rcss", sheetPath);
  const std::string documentPath = temp.write("health.rml", markup.c_str());

  Rml::Context *context =
      Rml::CreateContext("health", Rml::Vector2i(1280, 720));
  if (!context) {
    std::cerr << "FAIL: CreateContext\n";
    Rml::Shutdown();
    return 1;
  }

  TEST_CASE("document-loads");
  Rml::ElementDocument *document = context->LoadDocument(documentPath);
  CHECK(document != nullptr);
  if (document) {
    document->Show();
    context->Update();
    context->Render();

    TEST_CASE("zero-warnings");
    CHECK(systemInterface.Messages == 0);
    CHECK(systemInterface.Errors == 0);

    TEST_CASE("dom-ids");
    CHECK(hasId(document, "health"));
    CHECK(hasId(document, "health-icon"));
    CHECK(hasId(document, "health-label"));
    CHECK(hasId(document, "health-bar-bg"));
    CHECK(hasId(document, "health-bar"));
    CHECK(hasId(document, "armor-icon"));
    CHECK(hasId(document, "armor-label"));
    CHECK(hasId(document, "armor-bar-bg"));
    CHECK(hasId(document, "armor-bar"));
    CHECK(hasId(document, "longjump"));

    TEST_CASE("fill-lives-in-its-well");
    // The bar-fill-N percentage resolves against the well, so the well must
    // be the fill's containing block in the shipped document.
    Rml::Element *well = document->GetElementById("health-bar-bg");
    Rml::Element *fill = document->GetElementById("health-bar");
    CHECK(well != nullptr);
    CHECK(fill != nullptr);
    if (well && fill) {
      CHECK(fill->GetParentNode() == well);
      CHECK(fill->IsClassSet("bar-motion"));
      CHECK(fill->IsClassSet("bar-fill-0"));
      CHECK_NEAR(well->GetOffsetWidth(), 92.0, 0.5);
      CHECK_NEAR(well->GetOffsetHeight(), 5.0, 0.5);
    }

    TEST_CASE("ladder-resolves");
    // The module writes one class attribute per value change; a snap write
    // lands on the exact percentage of the well with no animation.
    if (fill) {
      fill->SetAttribute("class", "bar-motion bar-snap bar-fill-50");
      context->Update();
      CHECK_NEAR(fill->GetOffsetWidth(), 46.0, 0.5);
      fill->SetAttribute("class", "bar-motion bar-snap bar-fill-100");
      context->Update();
      CHECK_NEAR(fill->GetOffsetWidth(), 92.0, 0.5);
      fill->SetAttribute("class", "bar-motion bar-snap bar-fill-0");
      context->Update();
      CHECK_NEAR(fill->GetOffsetWidth(), 0.0, 0.5);
    }

    TEST_CASE("danger-color-stays-inline");
    // The danger hue shift is an inline background-color on the fill; the
    // ladder class only supplies the width.
    if (fill) {
      fill->SetProperty("background-color", "rgb(255, 0, 0)");
      context->Update();
      const Rml::Property *color = fill->GetLocalProperty("background-color");
      CHECK(color != nullptr);
    }

    TEST_CASE("hidden-slot-keeps-box");
    if (well) {
      const float width = well->GetOffsetWidth();
      well->SetProperty("visibility", "hidden");
      context->Update();
      CHECK_NEAR(well->GetOffsetWidth(), width, 0.5);
      well->SetProperty("visibility", "visible");
      context->Update();
    }

    document->Close();
  }

  Rml::RemoveContext("health");
  Rml::Shutdown();
  return abctest::finish("abc_ui_health_document");
}
