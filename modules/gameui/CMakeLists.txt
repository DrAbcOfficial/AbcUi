# abcui.gameui - the replacement game-menu pages (see sdk/abcui/abi.h for the
# module build contract). One folder per surface: shell (the main menu and the
# quit confirmation), console, serverbrowser, connection, vote, playerlist,
# commandmenu, inventory, creategame and settings; shared/ carries the borrowed
# api table and the text/DOM helpers every page repeats, while GameUiModule.cpp
# is the entry that validates the table before any page sees it. The settings
# surface is further split by responsibility (rows, bindings, video, voice,
# colours, previews, model picker, spray picker, menu preview, events,
# presentation) behind its own SettingsInternal.h seam. The menu preview's
# config model parses its module section with header-only rapidjson, so the
# module needs a third-party include path but never a framework link.
abcui_add_module(gameui
    SOURCES
        src/GameUiModule.cpp
        src/shell/MainMenuPage.cpp
        src/shell/QuitConfirmPage.cpp
        src/console/ConsolePage.cpp
        src/serverbrowser/ServerBrowserPage.cpp
        src/serverbrowser/ProjectionSignature.cpp
        src/connection/ConnectionPage.cpp
        src/vote/VoteMenuPage.cpp
        src/playerlist/PlayerListPage.cpp
        src/commandmenu/CommandMenuPage.cpp
        src/inventory/InventoryPage.cpp
        src/creategame/CreateGamePage.cpp
        src/settings/SettingsPage.cpp
        src/settings/SettingsRows.cpp
        src/settings/SettingsBindings.cpp
        src/settings/SettingsVideo.cpp
        src/settings/SettingsVoice.cpp
        src/settings/SettingsColors.cpp
        src/settings/SettingsPreviews.cpp
        src/settings/SettingsModelPicker.cpp
        src/settings/SettingsSpray.cpp
        src/settings/SettingsMenuPreview.cpp
        src/settings/MenuPreviewSettings.cpp
        src/settings/SettingsEvents.cpp
        src/settings/SettingsPresentation.cpp
        src/settings/SettingsModules.cpp
        src/settings/SettingsFileBrowser.cpp
    INCLUDES
        "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
