// abc_ui_division_document - the shipped abcui.division document parsed by
// the real RmlUi: every id the module drives must exist, the shipped RCSS and
// RML must produce zero warnings or errors, the row must lay out with a real
// width so the follow anchor has a box to clamp, and the segmented fills must
// resolve their bar-fill-N ladder against the cell they live in.
//
// The module package resolves assets through mod:// inside the engine. This
// test rewrites the three stylesheet links and the font family to repository
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

using abctest::hasId;
using abctest::readFile;
using abctest::replaceAll;

// The document carries the longjump icon; the null renderer returns empty
// texture handles, which RmlUi reports as a load failure. This renderer
// accepts every texture (the pixel data itself is never inspected offline).
} // namespace

int main() {
  abctest::WarningSystemInterface systemInterface;
  abctest::TextureRenderInterface renderer;
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
  const std::filesystem::path motionRcss =
      source / "modules/base/assets/rcss/barmotion.rcss";
  const std::filesystem::path divisionRcss = module / "rcss/division.rcss";
  const std::filesystem::path divisionRml = module / "rml/division.rml";
  const std::filesystem::path longjumpIcon = module / "img/longjump.png";

  abctest::TempDir temp("abcui-division-document");
  std::string base = readFile(baseRcss);
  std::string motion = readFile(motionRcss);
  std::string sheet = readFile(divisionRcss);
  CHECK(!motion.empty());
  replaceAll(base, "Verdana", "Lato");
  replaceAll(sheet, "Verdana", "Lato");
  const std::string basePath = temp.write("base.rcss", base.c_str());
  const std::string motionPath = temp.write("barmotion.rcss", motion.c_str());
  const std::string sheetPath = temp.write("division.rcss", sheet.c_str());

  std::string markup = readFile(divisionRml);
  CHECK(!markup.empty());
  CHECK(std::filesystem::is_regular_file(longjumpIcon));
  replaceAll(markup, "mod://abcui.base/rcss/base.rcss", basePath);
  replaceAll(markup, "mod://abcui.base/rcss/barmotion.rcss", motionPath);
  replaceAll(markup, "../rcss/division.rcss", sheetPath);
  replaceAll(markup, "mod://abcui.division/hud/img/longjump.png",
             longjumpIcon.generic_string());
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
    // Rendering is what actually fetches the icon through the file interface;
    // the accepting render interface above turns it into a handle without a
    // GPU.
    context->Render();

    TEST_CASE("zero-warnings");
    CHECK(systemInterface.Messages == 0);
    CHECK(systemInterface.Errors == 0);

    TEST_CASE("dom-ids");
    CHECK(hasId(document, "division"));
    CHECK(hasId(document, "cluster"));
    CHECK(hasId(document, "plate"));
    CHECK(hasId(document, "edge"));
    CHECK(hasId(document, "health-cells"));
    CHECK(hasId(document, "health-value"));
    CHECK(hasId(document, "armor-cells"));
    CHECK(hasId(document, "armor-value"));
    CHECK(hasId(document, "ammo-row1"));
    CHECK(hasId(document, "ammo-row2"));
    CHECK(hasId(document, "ammo-icon"));
    CHECK(hasId(document, "ammo2-icon"));
    CHECK(hasId(document, "clip1"));
    CHECK(hasId(document, "clip2"));
    CHECK(hasId(document, "ammo1"));
    CHECK(hasId(document, "ammo2"));
    CHECK(hasId(document, "flash-battery"));
    CHECK(hasId(document, "flash-fill"));
    CHECK(hasId(document, "flash-value"));
    CHECK(hasId(document, "longjump"));
    CHECK(hasId(document, "bullets"));

    TEST_CASE("bullet-slots");
    // One render element per particle slot.
    Rml::Element *layer = document->GetElementById("bullets");
    CHECK(layer != nullptr);
    if (layer)
      CHECK(layer->GetNumChildren() == 6);

    TEST_CASE("icon-loads");
    bool iconLoaded = false;
    for (const Rml::String &source : Rml::GetTextureSourceList())
      if (source.find("longjump.png") != Rml::String::npos)
        iconLoaded = true;
    CHECK(iconLoaded);

    TEST_CASE("third-person-lean");
    // The exact transform shape the module writes in third person must parse
    // without a warning.
    Rml::Element *tilted = document->GetElementById("cluster");
    CHECK(tilted != nullptr);
    if (tilted) {
      tilted->SetProperty("transform", "perspective(600dp) rotateY(12.0deg) "
                                       "skewX(6.0deg)");
      context->Update();
      context->Render();
      CHECK(systemInterface.Messages == 0);
    }

    TEST_CASE("number-pop");
    // The pop is an inline scale on the value element (moduleui::bounce): it
    // must parse, keep the laid-out box and resolve the sheet's alignment
    // edge as the scale origin.
    Rml::Element *value = document->GetElementById("health-value");
    CHECK(value != nullptr);
    if (value) {
      const float width = value->GetOffsetWidth();
      value->SetProperty("transform", "scale(1.280)");
      context->Update();
      context->Render();
      CHECK_NEAR(value->GetOffsetWidth(), width, 0.5);
      const Rml::Property *origin = value->GetProperty("transform-origin-x");
      CHECK(origin != nullptr);
      if (origin) {
        const Rml::NumericValue numeric = origin->GetNumericValue();
        CHECK(numeric.unit == Rml::Unit::PERCENT);
        CHECK_NEAR(numeric.number, 100.0, 0.01);
      }
      value->SetProperty("transform", "scale(1.000)");
      context->Update();
    }
    CHECK(systemInterface.Messages == 0);

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
    Rml::Element *bar = document->GetElementById("health-cells");
    CHECK(bar != nullptr);
    if (bar) {
      const float width = bar->GetOffsetWidth();
      CHECK_NEAR(width, 112.0, 0.5);
      bar->SetProperty("visibility", "hidden");
      context->Update();
      CHECK_NEAR(bar->GetOffsetWidth(), width, 0.5);
      bar->SetProperty("visibility", "visible");
      context->Update();
    }

    TEST_CASE("motion-classes");
    // The two bars and the battery well carry the shared motion class; the
    // module owns the direction token from then on.
    Rml::Element *healthCells = document->GetElementById("health-cells");
    Rml::Element *armorCells = document->GetElementById("armor-cells");
    Rml::Element *battery = document->GetElementById("flash-battery");
    Rml::Element *batteryFill = document->GetElementById("flash-fill");
    CHECK(healthCells && healthCells->IsClassSet("bar-motion"));
    CHECK(armorCells && armorCells->IsClassSet("bar-motion"));
    CHECK(battery && battery->IsClassSet("bar-motion"));
    CHECK(batteryFill && batteryFill->IsClassSet("fill"));
    CHECK(batteryFill && batteryFill->IsClassSet("bar-fill-0"));

    TEST_CASE("fill-ladder-resolves");
    // One cell as the module builds it: the fill's percentage resolves
    // against its own cell, which is what keeps a maximum change a pure
    // (un-animated) relayout.
    if (healthCells) {
      healthCells->SetInnerRML(
          "<div class=\"cell\"><div class=\"fill bar-fill-0\"></div></div>");
      Rml::Element *cell = healthCells->GetChild(0);
      Rml::Element *fill = cell ? cell->GetChild(0) : nullptr;
      CHECK(cell != nullptr);
      CHECK(fill != nullptr);
      if (cell && fill) {
        cell->SetProperty("left", "0dp");
        cell->SetProperty("width", "10dp");
        healthCells->SetAttribute("class", "bar-motion bar-snap");
        fill->SetAttribute("class", "fill bar-fill-50");
        context->Update();
        CHECK_NEAR(fill->GetOffsetWidth(), 5.0, 0.2);
        fill->SetAttribute("class", "fill bar-fill-100");
        context->Update();
        CHECK_NEAR(fill->GetOffsetWidth(), 10.0, 0.2);
      }
    }

    TEST_CASE("still-zero-warnings");
    CHECK(systemInterface.Messages == 0);
    CHECK(systemInterface.Errors == 0);

    document->Close();
  }

  Rml::RemoveContext("division");
  Rml::Shutdown();
  return abctest::finish("abc_ui_division_document");
}
