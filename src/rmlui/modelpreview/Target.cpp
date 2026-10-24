// modelpreview::Target - see Target.h.
//
// INTERMEDIATE STATE (issue #24, DI step): the old Renderer
// IEntityViewRenderer scope went away with the framework's Renderer
// dependency, and the replacement - the in-house engine V_RenderView route
// copied from MetaHookSv/StandaloneView - lands in its own commit. Until it
// does, every preview resolves to the existing "preview disabled" branch
// (the element hides its image); no GL or Renderer ownership leaks through
// this seam either way.
#include <metahook.h>
#include "Target.h"

#include <abc/Console.h>

#include "../AbcRenderInterface.h"
#include "../RmlUiModule.h"

#include <vector>

namespace modelpreview {
namespace {
std::vector<Target *> &targets() {
  static std::vector<Target *> Targets;
  return Targets;
}
bool Warned = false;
} // namespace

struct Target::State {
  std::string Uri;
};

Target::Target() : Data(std::make_unique<State>()) {
  static unsigned long long NextId = 0;
  Data->Uri = "modelfbo://" + std::to_string(++NextId) + "/";
  targets().push_back(this);
}

Target::~Target() {
  invalidate();
  std::erase(targets(), this);
}

void Target::invalidate() {
  if (auto *Interface = rmlui::renderInterface())
    Interface->unregisterExternalTexture(Data->Uri);
}

bool Target::render(const Options &Config, int Width, int Height) {
  (void)Config;
  (void)Width;
  (void)Height;
  if (!Warned) {
    Warned = true;
    abc::ConsoleWarn("[AbcUi model] preview disabled (the engine V_RenderView "
                     "route is not built yet)\n");
  }
  return false;
}

const std::string &Target::uri() const {
  return Data->Uri;
}
unsigned int Target::texture() const {
  return 0;
}
unsigned int Target::frames() const {
  return 0;
}
bool hasWorld() {
  return false;
}
void invalidateAll() {
  for (auto *Target : targets())
    Target->invalidate();
}
} // namespace modelpreview
