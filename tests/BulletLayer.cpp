#include "common/BulletLayer.h"
#include "framework/RmlTestHarness.h"
#include "framework/TestHarness.h"

#include <RmlUi/Core.h>

#include <cmath>

namespace {
Rml::Element *element(AbcUiElement *handle) {
  return reinterpret_cast<Rml::Element *>(handle);
}
} // namespace

int main() {
  abctest::QuietSystemInterface system;
  abctest::NullRenderInterface renderer;
  Rml::SetSystemInterface(&system);
  Rml::SetRenderInterface(&renderer);
  CHECK(Rml::Initialise());
  Rml::Context *context = Rml::CreateContext("bullets", {800, 600});
  auto *document = context->LoadDocumentFromMemory(
      "<rml><head><style>body {position:absolute;left:0;top:0;"
      "width:800px;height:600px;} #layer {position:absolute;left:0;top:0;}"
      "#layer div {position:absolute;left:0;top:0;width:20px;height:10px;"
      "display:none;}</style></head><body><div id='layer'>"
      "<div/><div/><div/><div/><div/><div/>"
      "</div></body></rml>");
  CHECK(document != nullptr);
  if (!document) {
    Rml::RemoveContext("bullets");
    Rml::Shutdown();
    return abctest::finish("abc_ui_bullet_layer");
  }
  document->Show();
  context->Update();
  auto *layer = document->GetElementById("layer");
  AbcUiUiApi ui{};
  ui.elementChild = [](AbcUiElement *parent, int32_t index) {
    return reinterpret_cast<AbcUiElement *>(element(parent)->GetChild(index));
  };
  ui.elementSetProperty = [](AbcUiElement *target, const char *name,
                             const char *value) {
    CHECK(element(target)->SetProperty(name, value));
  };
  AbcUiModuleApi api{};
  api.ui = &ui;
  moduleui::bulletfx::Bullets bullets;
  const auto paint = [&] {
    moduleui::bulletfx::paint(&api, reinterpret_cast<AbcUiElement *>(layer),
                              bullets);
    context->Update();
    context->Render();
  };

  TEST_CASE("first-frame-centred-without-measuring-hidden-element");
  const auto slot = bullets.fire({100.0f, 50.0f});
  CHECK(bullets.update(0.0f));
  paint();
  Rml::Element *round = layer->GetChild(static_cast<int>(slot));
  CHECK(round->IsVisible());
  Rml::Vector2f centre{100.0f, 50.0f};
  CHECK(round->Project(centre));
  CHECK_NEAR(centre.x, 10.0, 0.01);
  CHECK_NEAR(centre.y, 5.0, 0.01);
  CHECK_NEAR(round->GetProperty<float>("left"), 0.0, 0.0);
  CHECK_NEAR(round->GetProperty<float>("top"), 0.0, 0.0);

  TEST_CASE("reset-clears-dom-without-new-shots");
  document->Hide();
  bullets.clear();
  if (bullets.update(0.0f))
    paint();
  document->Show();
  context->Update();
  for (int index = 0; index < layer->GetNumChildren(); ++index)
    CHECK(!layer->GetChild(index)->IsVisible());
  CHECK(!bullets.update(0.016f));

  TEST_CASE("reused-slot-centres-after-reset");
  const auto next = bullets.fire({200.0f, 80.0f});
  CHECK(bullets.update(0.0f));
  paint();
  centre = {200.0f, 80.0f};
  CHECK(layer->GetChild(static_cast<int>(next))->Project(centre));
  CHECK_NEAR(centre.x, 10.0, 0.01);
  CHECK_NEAR(centre.y, 5.0, 0.01);
  Rml::RemoveContext("bullets");
  Rml::Shutdown();
  CHECK(system.Errors == 0);
  return abctest::finish("abc_ui_bullet_layer");
}
