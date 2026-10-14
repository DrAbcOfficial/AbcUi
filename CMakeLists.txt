cmake_minimum_required(VERSION 4.2)

project(AbcUi LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AbcCommon.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/MileFFmpeg.cmake")

set(ABC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/AbcCore")
add_subdirectory("${ABC_CORE_DIR}" AbcCore-build)

# Mile.FFmpeg binary SDK (video decode). Headers only in the build: the
# distance to Mile.FFmpeg.dll is GetProcAddress, exactly like Renderer's
# startup video; the DLL is deployed beside the one Renderer ships.
abcui_prepare_mile_ffmpeg(ABCUI_MILE_FFMPEG_SDK)

set(METAHOOK_INCLUDE_DIR "${ABC_CORE_DIR}/thirdparty/MetaHook/include")
set(VGUI2EXTENSION_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VGUI2Extension/include")
# Steamworks SDK (headers only - steamapi/ binds exports at runtime, no
# steam_api.lib import; see rmlui/SteamApiNative.cpp).
set(STEAMSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SteamSDK")

# --- FreeType (R-3) ---------------------------------------------------------
# Static FreeType built from the pinned submodule. RmlUi locates its font
# engine through find_package(Freetype), which cannot see a build-tree target;
# the soft-dependency check in RmlUi's CMake/Dependencies.cmake only tests
# TARGET Freetype::Freetype. So: disable the search, add_subdirectory freetype
# and alias it to the name RmlUi links (rmlui_core PRIVATE Freetype::Freetype).
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
add_subdirectory(thirdparty/freetype EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)

# --- RmlUi 6.3 static --------------------------------------------------------
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/RmlUi EXCLUDE_FROM_ALL)

# --- FreeImage (R-5) ---------------------------------------------------------
# FreeImage_clone's CMake hardcodes a SHARED library, so the static tier of
# D-06 is not reachable; the R-5 fallback applies: link the import lib and
# deploy the DLL next to the one Renderer already ships. MetaHook's launcher
# appends every metahook/dlls/* subdirectory to PATH, so the loader resolves
# FreeImage.dll from metahook/dlls/FreeImage at runtime (same mechanism
# Renderer.dll depends on). CRT is /MT inside the clone (self-contained DLL).
add_subdirectory(thirdparty/FreeImage EXCLUDE_FROM_ALL)

add_library(abc_ui_config STATIC
    src/services/uiconfig/UiDefinitions.cpp
    src/services/uiconfig/ConfigRepository.cpp
    src/services/serverbrowser/ServerBlacklist.cpp
    src/services/serverbrowser/BlacklistFileStore.cpp)
target_include_directories(abc_ui_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
target_compile_features(abc_ui_config PUBLIC cxx_std_20)

add_library(AbcUi MODULE
    src/plugins.cpp
    src/hooks.cpp
    src/rmlui/AbcRenderInterface.cpp
    src/rmlui/model/ElementModel.cpp
    src/rmlui/backdrop/ElementBackdrop.cpp
    src/rmlui/video/ElementVideo.cpp
    src/services/menubackdrop/Backdrop.cpp
    src/services/modelpreview/Options.cpp
    src/services/modelpreview/Target.cpp
    src/services/video/Decoder.cpp
    src/services/video/Options.cpp
    src/services/video/VideoTexture.cpp
    src/modules/modeltest/ModelTest.cpp
    src/modules/videotest/VideoTest.cpp
    src/rmlui/AbcSystemInterface.cpp
    src/rmlui/InputBridge.cpp
    src/rmlui/KeyMap.cpp
    src/rmlui/NativeEntryCallbacks.cpp
    src/rmlui/PaintGate.cpp
    src/rmlui/RmlUiModule.cpp
    src/rmlui/ProbeCommand.cpp
    src/rmlui/TextInput.cpp
    src/rmlui/SourceSpewStub.cpp
    src/rmlui/SteamAPI.cpp
    src/rmlui/SteamApiBridge.cpp
    src/rmlui/SteamApiNative.cpp
    src/rmlui/VGUI2ExtensionImport.cpp
    src/framework/Assets.cpp
    src/framework/Commands.cpp
    src/framework/FatalExit.cpp
    src/framework/TextEncoding.cpp
    src/framework/HudPanel.cpp
    src/framework/ImageLoader.cpp
    src/framework/MenuBackdrop.cpp
    src/framework/PreviewImage.cpp
    src/framework/InteractiveDocument.cpp
    src/framework/UiInputRouter.cpp
    src/framework/Localization.cpp
    src/data/Avatars.cpp
    src/data/HudState.cpp
    src/data/NetworkMessages.cpp
    src/data/PlayerResource.cpp
    src/data/PlayerTrace.cpp
    src/data/SpriteResource.cpp
    src/data/TextFeed.cpp
    src/data/TeamResource.cpp
    src/data/WeaponResource.cpp
    src/modules/UiModuleRegistry.cpp
    src/modules/gameui/GameUiModule.cpp
    src/modules/gameui/lifecycle/Frame.cpp
    src/modules/gameui/lifecycle/BackgroundWork.cpp
    src/modules/gameui/lifecycle/PresentationFrame.cpp
    src/modules/gameui/lifecycle/Documents.cpp
    src/modules/gameui/navigation/MenuPresentation.cpp
    src/modules/gameui/navigation/PageProjection.cpp
    src/modules/gameui/navigation/Actions.cpp
    src/modules/gameui/navigation/MenuAction.cpp
    src/modules/gameui/input/MenuInput.cpp
    src/modules/gameui/settings/SettingsFlow.cpp
    src/modules/gameui/settings/VideoFlow.cpp
    src/modules/gameui/serverbrowser/ServerBrowserFlow.cpp
    src/modules/gameui/creategame/CreateGameFlow.cpp
    src/modules/gameui/playerlist/PlayerListFlow.cpp
    src/modules/gameui/connection/LoadingFlow.cpp
    src/modules/gameui/connection/ConnectionActions.cpp
    src/modules/gameui/debug/DebugCommands.cpp
    src/modules/gameui/debug/SettingsCommands.cpp
    src/modules/gameui/debug/VideoCommands.cpp
    src/modules/gameui/debug/LoadingCommands.cpp
    src/modules/gameui/debug/ConsoleCommands.cpp
    src/modules/gameui/debug/NavigationCommands.cpp
    src/modules/gameui/debug/ServerCommands.cpp
    src/modules/gameui/debug/CreategameCommands.cpp
    src/modules/gameui/debug/PlayerlistCommands.cpp
    src/modules/gameui/engine/MenuBindings.cpp
    src/modules/gameui/engine/SettingsBindings.cpp
    src/modules/gameui/engine/VideoBindings.cpp
    src/modules/gameui/settings/SettingsResources.cpp
    src/modules/gameui/navigation/MenuController.cpp
    src/services/GameUiBridge.cpp
    src/services/steam/RuntimeAccess.cpp
    src/services/EngineUiBackend.cpp
    src/services/console/ConsoleBuffer.cpp
    src/services/console/ConsoleBackend.cpp
    src/services/serverbrowser/ServerQuery.cpp
    src/services/serverbrowser/ServerBrowserSession.cpp
    src/services/serverbrowser/ServerQuerySteam.cpp
    src/services/serverbrowser/ServerListModel.cpp
    src/services/serverbrowser/ServerFavorites.cpp
    src/services/creategame/CreateGameModel.cpp
    src/services/creategame/ServerOptions.cpp
    src/services/playerlist/PlayerListModel.cpp
    src/services/playerlist/MuteBackend.cpp
    src/services/playerlist/ClientMuteApi.cpp
    src/services/loading/LoadingModel.cpp
    src/services/settings/SettingsModel.cpp
    src/services/settings/KeyBindModel.cpp
    src/services/settings/VideoModeModel.cpp
    src/services/settings/VideoPendingStore.cpp
    src/services/settings/VoiceModel.cpp
    src/framework/PanelManager.cpp
    src/framework/PanelManagerLifecycle.cpp
    src/framework/Theme.cpp
    src/framework/StackBase.cpp
    src/framework/Projection.cpp
    src/panels/ammostack/AmmoStackPanel.cpp
    src/panels/ammobar/AmmoBarPanel.cpp
    src/panels/crosshair/CrosshairPanel.cpp
    src/panels/deadmsg/DeadMsgPanel.cpp
    src/panels/dmgtiles/DmgTilesPanel.cpp
    src/panels/effect/EffectPanel.cpp
    src/panels/gindicator/GIndicatorPanel.cpp
    src/panels/indicator/IndicatorPanel.cpp
    src/panels/itemhighlight/ItemHighlightPanel.cpp
    src/panels/notice/NoticePanel.cpp
    src/panels/playerboard/PlayerBoardPanel.cpp
    src/panels/vote/VotePanel.cpp
    src/panels/motd/MotdPanel.cpp
    src/panels/mainmenu/MainMenuPage.cpp
    src/panels/menushared/MenuShellWindows.cpp
    src/panels/loading/LoadingWindows.cpp
    src/panels/console/ConsolePage.cpp
    src/panels/serverbrowser/ServerBrowserPage.cpp
    src/panels/serverbrowser/ServerBrowserControls.cpp
    src/panels/creategame/CreateGamePage.cpp
    src/panels/creategame/CreateGameOptions.cpp
    src/panels/playerlist/PlayerListPage.cpp
    src/panels/settings/SettingsUi.cpp
    src/panels/settings/sections/VideoSection.cpp
    src/panels/settings/sections/VoiceSection.cpp
    src/panels/settings/sections/PreviewSection.cpp
    src/panels/settings/SettingsPage.cpp
    src/panels/settings/SettingsNavigation.cpp
    src/panels/settings/SettingsEvents.cpp
    src/panels/settings/sections/CvarRows.cpp
    src/panels/settings/sections/BindingRows.cpp
    src/panels/settings/VideoConfirmPage.cpp
    src/panels/scoreboard/ScoreboardPanel.cpp
    src/panels/weaponchoose/WeaponChoosePanel.cpp
    src/panels/popnum/PopNumPanel.cpp
    src/panels/radar/RadarPanel.cpp
    src/panels/sidetext/SideTextPanel.cpp
    src/panels/textmenu/TextMenuPanel.cpp
    src/panels/flashlight/FlashlightPanel.cpp
    src/panels/health/HealthPanel.cpp
    src/panels/itemstack/ItemStackPanel.cpp
    src/panels/weaponstack/WeaponStackPanel.cpp
    # Generic CreateInterface export (EXPOSE_SINGLE_INTERFACE implementation)
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
)
target_compile_definitions(AbcUi PRIVATE WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS)
# imm32: T07 IME placement/cancel (ImmSetCompositionWindow / ImmNotifyIME).
target_link_libraries(AbcUi PRIVATE Abc::Core RmlUi::RmlUi FreeImage imm32 abc_ui_config)

include("${ABC_CORE_DIR}/cmake/AbcGameData.cmake")
abc_add_gamedata(AbcUi "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manifests/abcui.json")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(AbcUiLayoutGameData
    COMMAND "${Python3_EXECUTABLE}"
        "${ABC_CORE_DIR}/thirdparty/Renderer/scripts/validate-gamedata.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/gamedata/abcui-layout"
    COMMENT "Validating binary-specific AbcUi native layouts"
    VERBATIM)
add_dependencies(AbcUi AbcUiLayoutGameData)
if(ABC_GAME_DIR)
    set(ABC_UI_LAYOUT_DEST "${ABC_GAME_DIR}/svencoop/metahook/gamedata/abcui-layout")
else()
    set(ABC_UI_LAYOUT_DEST "svencoop/metahook/gamedata/abcui-layout")
endif()
install(DIRECTORY assets/gamedata/abcui-layout/ DESTINATION "${ABC_UI_LAYOUT_DEST}")
# Include layout mirrors Renderer's renderer_settings interface set: the
# VGUI2Extension interface headers include MetaHook VGUI/SourceSDK headers by
# bare name (<VGUI.h>, <Color.h>, <mathlib/vector2d.h>).
target_include_directories(AbcUi PRIVATE
    "${ABCUI_MILE_FFMPEG_SDK}/Include"
    "${STEAMSDK_DIR}"
    "${VGUI2EXTENSION_INCLUDE_DIR}"
    "${VGUI2EXTENSION_INCLUDE_DIR}/Interface"
    "${VGUI2EXTENSION_INCLUDE_DIR}/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/VGUI"
    "${METAHOOK_INCLUDE_DIR}/SourceSDK"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(ABC_GAME_DIR)
    install(TARGETS AbcUi
        RUNTIME DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins"
        LIBRARY DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins")
    # FreeImage.dll lives with Renderer's copy (metahook/dlls/FreeImage); the
    # launcher puts that directory on PATH for import resolution.
    install(FILES "$<TARGET_FILE:FreeImage>"
        DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/dlls/FreeImage")
    # Video decode runtime: same drop point convention as Renderer's copy so
    # the launcher's PATH addition resolves it for both plugins.
    install(FILES "${ABCUI_MILE_FFMPEG_SDK}/Redist/x86/Mile.FFmpeg.dll"
        DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/dlls/Mile.FFmpeg")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/abcui")
        install(DIRECTORY assets/abcui DESTINATION "${ABC_GAME_DIR}/svencoop"
            PATTERN ".gitkeep" EXCLUDE)
    endif()
    add_custom_target(deploy
        DEPENDS AbcUi FreeImage
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        COMMENT "Deploying AbcUi.dll, FreeImage.dll and assets to ${ABC_GAME_DIR}")
endif()
