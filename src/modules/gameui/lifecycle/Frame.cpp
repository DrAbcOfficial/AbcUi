// Composition root of frame execution: services first, presentation second.
#include "modules/gameui/GameUiModule.h"
#include "FramePump.h"
namespace gameui {
void GameUiModule::onFrame(double FrameTime) {
    (void)FrameTime;
    lifecycle::pumpMenuFrame([this] { pumpBackgroundWork(); },
        [this] { return MenuPresented && Shell && Context; },
        [this] { pumpVisibleWork(); });
}
}
