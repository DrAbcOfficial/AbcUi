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

# The key-code rows (the vote answer keys): the schema's float-only "key"
# flag and the plain float transaction that persists the captured key number.
abcui_add_test(abc_ui_settings_key_row
    SOURCES SettingsKeyRow.cpp
            "${SRC}/services/uiconfig/UiDefinitions.cpp"
            "${SRC}/services/settings/SettingsModel.cpp"
    LIBRARIES Abc::Core)

# The shell's menu definition (uiconfig's mainmenu.json): the fixed intents
# plus the "command" entries that carry their own console line.
abcui_add_test(abc_ui_menu_shell
    SOURCES MenuShell.cpp
            "${SRC}/services/uiconfig/UiDefinitions.cpp"
            "${SRC}/services/settings/SettingsModel.cpp"
    LIBRARIES Abc::Core)

# The create-game server option document (server_options.json) and the saved
# values in config.json's create_game section: the packaged rows, the strict
# shape and value gate, the saved round trip and the shipped file itself.
abcui_add_test(abc_ui_create_game_settings
    SOURCES CreateGameSettings.cpp
            "${SRC}/services/creategame/ServerOptions.cpp"
            "${SRC}/services/creategame/ServerSettings.cpp"
    LIBRARIES Abc::Core)

# The weapon selector layout and the module's rule model: override
# precedence, collision shifting inside the slot, clamping,
# registration-order independence, JSON validation and the serialization
# round trip (pure, no engine).
abcui_add_test(abc_ui_weapon_filter
    SOURCES WeaponFilter.cpp
            "${SRC}/data/WeaponSlotLayout.cpp"
            "${PROJECT_SOURCE_DIR}/modules/weaponfilter/src/WeaponFilterModel.cpp"
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The abcui.weaponfilter module driven through recording config, console and
# weapons seams: cvar watch, the five commands, reload and the read-only
# damaged-file session.
abcui_add_test(abc_ui_weapon_filter_module
    SOURCES WeaponFilterModule.cpp
            "${PROJECT_SOURCE_DIR}/modules/weaponfilter/src/WeaponFilterModule.cpp"
            "${PROJECT_SOURCE_DIR}/modules/weaponfilter/src/WeaponFilterModel.cpp"
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules"
    DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN)

# The connection surfaces (the four abcui.gameui documents) driven through
# recording documents/ui/events/menu/connection seams: listeners bind on the
# descriptor's onReady (a module boots before any Rml context, so a load-time
# root() is null), each dialog button reaches its typed intent, and a context
# cycle rebinds through onClose/onReady (issue #133).
abcui_add_test(abc_ui_connection_page
    SOURCES ConnectionPage.cpp
            "${PROJECT_SOURCE_DIR}/modules/gameui/src/connection/ConnectionPage.cpp"
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules"
                 "${PROJECT_SOURCE_DIR}/modules/gameui/src"
    DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN)

# Module-scoped config keys: the local-name grammar, key composition and the
# section bound the config ABI enforces.
abcui_add_test(abc_ui_module_config
    SOURCES ModuleConfig.cpp
            "${SRC}/services/userconfig/ModuleConfig.cpp")

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

# The third-person lean rebuilt from the applied plate (header-only).
abcui_add_test(abc_ui_lean
    SOURCES DivisionLean.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The 10-point cell arithmetic behind the group's segmented bars.
abcui_add_test(abc_ui_segment_bar
    SOURCES SegmentBar.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The shared smooth-bar policy: width ladder, direction tokens and the write
# tracker both vitals groups drive (header-only).
abcui_add_test(abc_ui_bar_motion
    SOURCES BarMotion.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The shipped shared bar ramp driven like the modules drive it (fake clock).
abcui_add_test(abc_ui_bar_animation
    SOURCES BarAnimation.cpp
    LIBRARIES RmlUi::RmlUi)

# The shared number pop behind the vitals and magazine readouts: peak frame,
# eased return, restart and reset rules (header-only).
abcui_add_test(abc_ui_value_bounce
    SOURCES ValueBounce.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

abcui_add_test(abc_ui_opacity_motion
    SOURCES OpacityMotion.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# Exercise each production module against the same recording ABI seams.
foreach(module IN ITEMS Ammo Flashlight HudEffects KillFeed Motd TextMenu)
    string(TOLOWER "${module}" module_id)
    abcui_add_test("abc_ui_${module_id}_opacity"
        SOURCES HudOpacity.cpp
                "${PROJECT_SOURCE_DIR}/modules/${module_id}/src/${module}Module.cpp"
                "${SRC}/modsys/Arbitration.cpp"
                "${SRC}/modsys/Facts.cpp"
        DEFINITIONS "ABCUI_TEST_MODULE=\"${module_id}\"" NOMINMAX
                    WIN32_LEAN_AND_MEAN)
endforeach()

# Weapon selector input ownership and empty-slot presentation.
# Rename only the test-linked entry so both production modules can coexist.
add_library(abcui_test_textmenu_entry OBJECT
    "${PROJECT_SOURCE_DIR}/modules/textmenu/src/TextMenuModule.cpp")
target_include_directories(abcui_test_textmenu_entry PRIVATE
    "${PROJECT_SOURCE_DIR}/sdk")
target_compile_features(abcui_test_textmenu_entry PRIVATE cxx_std_20)
target_compile_definitions(abcui_test_textmenu_entry PRIVATE
    AbcUiModule_Main=AbcUiTextMenu_Main)

abcui_add_test(abc_ui_weaponchoose
    SOURCES WeaponChoose.cpp
            "${PROJECT_SOURCE_DIR}/modules/weaponchoose/src/WeaponChooseModule.cpp"
            "${SRC}/modsys/Arbitration.cpp"
            "${SRC}/modsys/Facts.cpp"
            $<TARGET_OBJECTS:abcui_test_textmenu_entry>)

abcui_add_test(abc_ui_weaponchoose_textmenu_first
    SOURCES WeaponChoose.cpp
            "${PROJECT_SOURCE_DIR}/modules/weaponchoose/src/WeaponChooseModule.cpp"
            "${SRC}/modsys/Arbitration.cpp"
            "${SRC}/modsys/Facts.cpp"
            $<TARGET_OBJECTS:abcui_test_textmenu_entry>
    DEFINITIONS ABCUI_TEST_TEXTMENU_FIRST)

# The native text-menu fallback: no textmenu renderer, the menu fact still
# gates the weapon selector and the slot command reaches the engine original.
abcui_add_test(abc_ui_weaponchoose_native
    SOURCES WeaponChoose.cpp
            "${PROJECT_SOURCE_DIR}/modules/weaponchoose/src/WeaponChooseModule.cpp"
            "${SRC}/modsys/Arbitration.cpp"
            "${SRC}/modsys/Facts.cpp"
            $<TARGET_OBJECTS:abcui_test_textmenu_entry>
    DEFINITIONS ABCUI_TEST_TEXTMENU_ABSENT)

# Entry ownership: order independence, fallback/observer roles, gates, facts.
abcui_add_test(abc_ui_arbitration
    SOURCES Arbitration.cpp
            "${SRC}/modsys/Arbitration.cpp"
            "${SRC}/modsys/Facts.cpp")

# The fixed-capacity particle system behind the HUD effects.
abcui_add_test(abc_ui_particle_system
    SOURCES ParticleSystem.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

# The fired-round effect built on it, shared by both ammo readouts.
abcui_add_test(abc_ui_bullet_fx
    SOURCES BulletFx.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules")

abcui_add_test(abc_ui_bullet_layer
    SOURCES BulletLayer.cpp
    INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules"
    LIBRARIES RmlUi::RmlUi)

# The shipped group document: ids, RCSS grammar and a real laid-out row.
abcui_add_test(abc_ui_division_document
    SOURCES DivisionDocument.cpp
    LIBRARIES RmlUi::RmlUi)

# The shipped classic vitals document: ids, the nested meter fill and the
# width ladder resolving against its well.
abcui_add_test(abc_ui_health_document
    SOURCES HealthDocument.cpp
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
            "${SRC}/modsys/Arbitration.cpp"
            "${SRC}/modsys/Facts.cpp"
            "${SRC}/modsys/HookRegistry.cpp"
            "${SRC}/modsys/EventBus.cpp"
    LIBRARIES Abc::Core)

abcui_add_test(abc_ui_inventory_model
    SOURCES InventoryModel.cpp "${SRC}/services/inventory/InventoryModel.cpp"
    LIBRARIES Abc::Core)

# The prediction weapon domain: the SDK 256-entry array is the registration
# boundary for server weapon ids (ids 64..255 must stay accepted).
abcui_add_test(abc_ui_weapon_domain
    SOURCES WeaponDomain.cpp)

# UserMsg field quirks: HideHUD is a short, AmmoX is absolute and MapList has
# a payload-less end opcode.
abcui_add_test(abc_ui_user_msg_fields
    SOURCES UserMsgFields.cpp)

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

