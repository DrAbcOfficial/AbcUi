// Real HudPanel/Rml document + real PanelManager session pump. Only the
// engine panel factory and game state seam are substituted.
#include "framework/HudPanel.h"
#include "framework/PanelManager.h"
#include "framework/UiInputRouter.h"
#include <RmlUi/Core.h>
#include <cstdlib>
#include <iostream>
#include "framework/TestHarness.h"
#include "framework/RmlTestHarness.h"

namespace hudstate {
bool isHudHidden(int) {
  return false;
}
} // namespace hudstate
namespace framework {
std::vector<std::unique_ptr<HudPanel>> &PanelManager::Panels() {
  static std::vector<std::unique_ptr<HudPanel>> panels;
  return panels;
}
} // namespace framework
class Renderer : public Rml::RenderInterface {
public:
  int Draws = 0;
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                              Rml::Span<const int>) override {
    return 1;
  }
  void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                      Rml::TextureHandle) override {
    ++Draws;
  }
  void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i &,
                                 const Rml::String &) override {
    return {};
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,
                                     Rml::Vector2i) override {
    return {};
  }
  void ReleaseTexture(Rml::TextureHandle) override {}
  void EnableScissorRegion(bool) override {}
  void SetScissorRegion(Rml::Rectanglei) override {}
};
struct Sink : framework::uiinput::Sink {
  void uiKeyDown(int) override {}
  void uiKeyUp(int) override {}
  void onExclusiveChange(bool) override {}
};
struct Owner : framework::uiinput::Owner {
  bool claimsKey(int, int) override {
    return true;
  }
  bool claimsWheel() override {
    return true;
  }
};
class TestPanel : public framework::HudPanel {
public:
  TestPanel()
      : HudPanel("session-test", (abctest::sourceDir() /
                                  "modules/health/assets/hud/rml/health.rml")
                                     .string()
                                     .c_str()) {}
  int DataFrames = 0, Redraws = 0, Resets = 0;
  framework::uiinput::Token PointerToken;
  void onData(double) override {
    ++DataFrames;
    show();
  }
  void onRedraw(double) override {
    ++Redraws;
    show();
  }
  void reset() override {
    ++Resets;
    PointerToken.release();
    show(); // even a badly behaved reset must not resurrect a HUD
  }
  void onDocumentReady() override {
    // Use a plain colored quad to prove actual rendering, no font/assets.
    Document->SetInnerRML(
        "<div style=\"width: 40px; height: 40px; background-color: #f00;\"/>");
    show(); // event-driven initial request before session activation
  }
};

int main() {
  Renderer renderer;
  Rml::SystemInterface system;
  Rml::SetRenderInterface(&renderer);
  Rml::SetSystemInterface(&system);
  CHECK(Rml::Initialise());
  auto *context = Rml::CreateContext("session", {640, 480});
  CHECK(context);
  using framework::PanelManager;
  // Test-only factory injection; production owns the vector identically.
  auto &panels =
      const_cast<std::vector<std::unique_ptr<framework::HudPanel>> &>(
          PanelManager::panels());
  panels.push_back(std::make_unique<TestPanel>());
  auto &panel = static_cast<TestPanel &>(*panels.front());
  CHECK(panel.loadDocument(context));
  CHECK(!panel.isVisible());
  PanelManager::onFrame(0.01);
  PanelManager::onRedraw(0.01);
  CHECK(panel.DataFrames == 0 && panel.Redraws == 0);
  PanelManager::setSessionActive(true);
  CHECK(panel.isVisible()); // initial event request preserved
  PanelManager::onFrame(0.01);
  PanelManager::onRedraw(0.01);
  CHECK(panel.DataFrames == 1 && panel.Redraws == 1);
  context->Update();
  context->Render(); // the redraw path must not crash without styled geometry

  Sink sink;
  framework::uiinput::Router router(&sink);
  Owner hud, menu;
  auto menuToken = router.acquire(&menu, framework::uiinput::OwnerTier::kMenu);
  panel.PointerToken =
      router.acquire(&hud, framework::uiinput::OwnerTier::kInteractiveHud);
  auto *menuDoc =
      context->LoadDocumentFromMemory("<rml><head></head><body/></rml>");
  CHECK(menuDoc);
  menuDoc->Show();
  PanelManager::setSessionActive(false); // disconnect without HUD_Shutdown
  CHECK(!panel.isVisible() && panel.Resets == 1 && !panel.PointerToken.held());
  CHECK(menuToken.held() && menuDoc->IsVisible()); // menu survives
  panel.show(); // late network-driven show is gated too
  CHECK(!panel.isVisible());
  PanelManager::onFrame(0.01);
  PanelManager::onRedraw(0.01);
  CHECK(panel.DataFrames == 1 && panel.Redraws == 1);
  renderer.Draws = 0;
  context->Render(); // no intervening HUD_Frame/Context::Update required
  CHECK(renderer.Draws == 0);
  PanelManager::setSessionActive(false);
  CHECK(panel.Resets == 1);
  panel.hide(); // discard the deliberate late-show test request
  PanelManager::setSessionActive(true); // reconnect
  CHECK(!panel.isVisible());
  PanelManager::onFrame(0.01);
  CHECK(panel.isVisible() && panel.DataFrames == 2);
  PanelManager::setSessionActive(false);
  CHECK(panel.Resets == 2 && !panel.isVisible());
  menuToken.release();
  panels.clear();
  Rml::Shutdown();
  std::cout << "PASS: HUD session visibility, frame gates, disconnect reset, "
               "menu isolation, reconnect\n";
  return abctest::finish("abc_ui_hud_session");
}
