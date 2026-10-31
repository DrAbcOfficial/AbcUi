// abc_ui_division_document - the shipped abcui.division document parsed by
// the real RmlUi: every id the module drives must exist, the shipped RCSS and
// RML must produce zero warnings or errors, and the row must lay out with a
// real width so the follow anchor has a box to clamp.
//
// The module package resolves assets through mod:// inside the engine. This
// test rewrites the two stylesheet links and the font family to repository
// paths / the bundled test font (the same substitution the other offline page
// tests rely on); nothing else about the document changes.
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
  abctest::NullRenderInterface renderer;
  Rml::SetSystemInterface(&systemInterface);
  Rml::SetRenderInterface(&renderer);
  if (!Rml::Initialise()) {
    std::cerr << "FAIL: Rml::Initialise\n";
    return 1;
  }
  // The row's numerals are bold; the bundled Lato regular face alone would
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
  const std::filesystem::path module = source / "modules/division/assets/hud";
  const std::filesystem::path baseRcss =
      source / "modules/base/assets/rcss/base.rcss";
  const std::filesystem::path divisionRcss = module / "rcss/division.rcss";
  const std::filesystem::path divisionRml = module / "rml/division.rml";

  abctest::TempDir temp("abcui-division-document");
  std::string base = readFile(baseRcss);
  std::string sheet = readFile(divisionRcss);
  replaceAll(base, "Verdana", "Lato");
  replaceAll(sheet, "Verdana", "Lato");
  const std::string basePath = temp.write("base.rcss", base.c_str());
  const std::string sheetPath = temp.write("division.rcss", sheet.c_str());

  std::string markup = readFile(divisionRml);
  CHECK(!markup.empty());
  replaceAll(markup, "mod://abcui.base/rcss/base.rcss", basePath);
  replaceAll(markup, "../rcss/division.rcss", sheetPath);
  const std::string documentPath = temp.write("division.rml", markup.c_str());

  Rml::Context *context =
      Rml::CreateContext("division", Rml::Vector2i(1280, 720));
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

    TEST_CASE("zero-warnings");
    CHECK(systemInterface.Messages == 0);
    CHECK(systemInterface.Errors == 0);

    TEST_CASE("dom-ids");
    CHECK(hasId(document, "division"));
    CHECK(hasId(document, "cluster"));
    CHECK(hasId(document, "vitals"));
    CHECK(hasId(document, "health"));
    CHECK(hasId(document, "health-value"));
    CHECK(hasId(document, "health-fill"));
    CHECK(hasId(document, "armor"));
    CHECK(hasId(document, "armor-value"));
    CHECK(hasId(document, "armor-fill"));
    CHECK(hasId(document, "ammo"));
    CHECK(hasId(document, "ammo-icon"));
    CHECK(hasId(document, "ammo-clip"));
    CHECK(hasId(document, "ammo-reserve"));
    CHECK(hasId(document, "flash"));
    CHECK(hasId(document, "flash-value"));
    CHECK(hasId(document, "flash-fill"));

    TEST_CASE("row-lays-out");
    Rml::Element *cluster = document->GetElementById("cluster");
    CHECK(cluster != nullptr);
    if (cluster) {
      CHECK(cluster->GetOffsetWidth() > 0.0f);
      CHECK(cluster->GetOffsetHeight() > 0.0f);
    }

    TEST_CASE("hidden-slot-keeps-box");
    // The module hides slots through visibility; the box (and therefore the
    // row width the anchor clamps against) must survive.
    Rml::Element *armor = document->GetElementById("armor");
    CHECK(armor != nullptr);
    if (armor) {
      const float width = armor->GetOffsetWidth();
      armor->SetProperty("visibility", "hidden");
      context->Update();
      CHECK_NEAR(armor->GetOffsetWidth(), width, 0.5);
      armor->SetProperty("visibility", "visible");
      context->Update();
    }

    document->Close();
  }

  Rml::RemoveContext("division");
  Rml::Shutdown();
  return abctest::finish("abc_ui_division_document");
}
