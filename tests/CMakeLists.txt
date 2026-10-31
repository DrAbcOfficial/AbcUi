# Offline verification suite.
#
# The suite compiles production translation units against recording seams and
# static RmlUi; it needs no game installation, network access, local path or
# environment variable. Configure with -DABCUI_BUILD_TESTS=ON and run
#   ctest --test-dir <build-dir> -C <Config> --output-on-failure
#
# Shared conventions live in framework/: TestHarness.h owns the assertion and
# exit contract, TestEnvironment.h resolves the repository asset root and the
# RmlUi test fonts through generated (never committed) configure-time paths.

set(SRC "${PROJECT_SOURCE_DIR}/src")

set(VGUI2EXTENSION_SRC_DIR "${PROJECT_SOURCE_DIR}/thirdparty/VGUI2Extension")

set(ABCUI_TEST_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${SRC}"
    "${PROJECT_SOURCE_DIR}/sdk"
    "${ABC_CORE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/thirdparty/miniz"
    "${ABCUI_MINIZ_GEN_DIR}"
    "${METAHOOK_INCLUDE_DIR}"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/engine"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/cl_dll"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/public"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/pm_shared"
    "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")

set(ABCUI_TEST_VGUI_INCLUDE_DIRS
    "${VGUI2EXTENSION_SRC_DIR}/src"
    "${VGUI2EXTENSION_SRC_DIR}/include/Interface"
    "${VGUI2EXTENSION_SRC_DIR}/include/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/VGUI"
    "${METAHOOK_INCLUDE_DIR}/vgui_controls"
    "${METAHOOK_INCLUDE_DIR}/SourceSDK"
    "${STEAMSDK_DIR}")

# The fonts are part of the pinned RmlUi submodule. Fail at configure time
# instead of silently skipping the layout checks when a submodule is missing.
set(ABCUI_TEST_FONT_SANS
    "${PROJECT_SOURCE_DIR}/thirdparty/RmlUi/Samples/assets/LatoLatin-Regular.ttf")
set(ABCUI_TEST_FONT_MONO
    "${PROJECT_SOURCE_DIR}/thirdparty/RmlUi/Samples/assets/RobotoMono-Regular.ttf")
if(NOT EXISTS "${ABCUI_TEST_FONT_SANS}" OR NOT EXISTS "${ABCUI_TEST_FONT_MONO}")
    message(FATAL_ERROR
        "The offline tests need the RmlUi sample fonts; initialize thirdparty/RmlUi with git submodule update --init --recursive")
endif()
set(ABCUI_TEST_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/framework/TestBuildConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/TestBuildConfig.h" @ONLY)

include("${CMAKE_CURRENT_LIST_DIR}/TestSupport.cmake")

add_library(abcui_test_support STATIC
    framework/TestHarness.cpp
    framework/TestEnvironment.cpp)
target_include_directories(abcui_test_support PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(abcui_test_support PUBLIC cxx_std_20)

# The suite links AbcCore dynamically (Abc::Core). All test executables share
# one output directory, so a single copy target keeps AbcCore.dll next to every
# test for every configuration.
add_custom_target(abcui_test_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Abc::Core>"
        "$<TARGET_FILE_DIR:abcui_test_support>/AbcCore.dll"
    COMMENT "Copying AbcCore.dll next to the offline test executables"
    VERBATIM)
add_dependencies(abcui_test_runtime abc_core)

# --- model and service tests -------------------------------------------------
abcui_add_test(abc_ui_helpers
    SOURCES UiHelpers.cpp
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_modules
    SOURCES ModuleRegistry.cpp "${SRC}/modules/UiModuleRegistry.cpp"
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_menu_state
    SOURCES MenuStateMachine.cpp
            "${SRC}/modules/gameui/navigation/MenuController.cpp")

abcui_add_test(abc_ui_text_encoding
    SOURCES TextEncoding.cpp "${SRC}/framework/TextEncoding.cpp")

abcui_add_test(abc_ui_keybind_model
    SOURCES KeyBindModel.cpp "${SRC}/services/settings/KeyBindModel.cpp")

abcui_add_test(abc_ui_video_mode
    SOURCES VideoModeModel.cpp "${SRC}/services/settings/VideoModeModel.cpp"
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_voice_model
    SOURCES VoiceModel.cpp "${SRC}/services/settings/VoiceModel.cpp")

abcui_add_test(abc_ui_loading_model
    SOURCES LoadingModel.cpp
            "${SRC}/services/loading/LoadingModel.cpp"
            "${SRC}/services/GameUiBridge.cpp"
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_server_query
    SOURCES ServerQuery.cpp "${SRC}/services/serverbrowser/ServerQuery.cpp")

# Module-side helper shared by the third-person HUD group (header-only).
abcui_add_test(abc_ui_follow_anchor
    SOURCES DivisionFollow.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The shipped group document: ids, RCSS grammar and a real laid-out row.
abcui_add_test(abc_ui_division_document
    SOURCES DivisionDocument.cpp
    LIBRARIES RmlUi::RmlUi)

abcui_add_test(abc_ui_audit_regressions
    SOURCES AuditRegressions.cpp
            "${SRC}/modules/gameui/navigation/MenuAction.cpp"
            "${SRC}/framework/UiInputRouter.cpp"
            "${SRC}/services/console/ConsoleBackend.cpp"
            "${SRC}/services/serverbrowser/ServerQuery.cpp"
            "${SRC}/services/serverbrowser/ServerListModel.cpp"
            "${SRC}/services/serverbrowser/ServerBlacklist.cpp"
            "${SRC}/services/serverbrowser/ServerBrowserSession.cpp"
            "${SRC}/services/settings/VideoModeModel.cpp"
            "${SRC}/services/playerlist/PlayerListModel.cpp")

abcui_add_test(abc_ui_ping_refresh
    SOURCES PingRefresh.cpp
            "${SRC}/data/PlayerResource.cpp"
            "${SRC}/data/NetworkMessages.cpp"
            "${SRC}/modsys/EventBus.cpp"
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_inventory_model
    SOURCES InventoryModel.cpp "${SRC}/services/inventory/InventoryModel.cpp"
    LIBRARIES Abc::Core)

# --- RmlUi page and document tests -------------------------------------------
abcui_add_test(abc_ui_interactive
    SOURCES InteractiveDocument.cpp
            "${SRC}/framework/InteractiveDocument.cpp"
            "${SRC}/framework/MenuBackdrop.cpp"
            "${SRC}/framework/Localization.cpp"
            "${SRC}/modsys/ModuleVfs.cpp"
            "${SRC}/modsys/PackageFile.cpp"
            "${SRC}/modsys/PackageImage.cpp"
            "${SRC}/rmlui/LanguageChannel.cpp"
    LIBRARIES RmlUi::RmlUi Abc::Core abcui_miniz)

abcui_add_test(abc_ui_text_input
    SOURCES TextInput.cpp
            "${SRC}/rmlui/TextInput.cpp"
            "${SRC}/rmlui/ImeOverlay.cpp"
            "${SRC}/rmlui/LanguageChannel.cpp"
            "${SRC}/framework/TextEncoding.cpp"
            "${SRC}/framework/InteractiveDocument.cpp"
            "${SRC}/framework/MenuBackdrop.cpp"
            "${SRC}/framework/Localization.cpp"
            "${SRC}/modsys/ModuleVfs.cpp"
            "${SRC}/modsys/PackageFile.cpp"
            "${SRC}/modsys/PackageImage.cpp"
    LIBRARIES RmlUi::RmlUi Abc::Core abcui_miniz imm32
    DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN)

abcui_add_test(abc_ui_hud_session
    SOURCES HudSession.cpp
            "${SRC}/framework/HudPanel.cpp"
            "${SRC}/framework/PanelManagerLifecycle.cpp"
            "${SRC}/framework/UiInputRouter.cpp"
    LIBRARIES RmlUi::RmlUi Abc::Core)

# --- entry / VGUI2 integration tests -----------------------------------------
abcui_add_test(abc_ui_hook_contract
    SOURCES HookContract.cpp
            "${VGUI2EXTENSION_SRC_DIR}/src/VGUI2ExtensionInternal.cpp"
            "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
    VGUI)

abcui_add_test(abc_ui_console
    SOURCES ConsoleService.cpp
            "${SRC}/services/console/ConsoleBuffer.cpp"
            "${SRC}/services/console/ConsoleBackend.cpp"
            "${SRC}/framework/TextEncoding.cpp"
            "${VGUI2EXTENSION_SRC_DIR}/src/VGUI2ExtensionInternal.cpp"
            "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
    VGUI)

