// Composition root of frame execution: services first, presentation second.
#include "modules/gameui/GameUiModule.h"
#include "FramePump.h"
#include "services/menumusic/MenuMusic.h"
#include <abc/CvarCmd.h>
namespace gameui {
void GameUiModule::onFrame(double FrameTime) {
    // Menu score: plays only while a replacement page is on screen, so the
    // track never bleeds into gameplay (the bridge knows the presented fact).
    auto& music = menumusic::menuMusic();
    if (cvar_t* source = abc::CVAR_GET_POINTER("abc_ui_menu_music")) {
        if (source->string)
            music.setSource(source->string);
    }
    if (cvar_t* level = abc::CVAR_GET_POINTER("abc_ui_menu_music_volume"))
        music.setVolume(level->value);
    music.update(FrameTime, services::nativeUiBridge().pageShown());
    lifecycle::pumpMenuFrame([this] { pumpBackgroundWork(); },
        [this] { return MenuPresented && Shell && Context; },
        [this] { pumpVisibleWork(); });
}
}
