add_executable(abc_ui_helpers UiHelpers.cpp)
target_include_directories(abc_ui_helpers PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_helpers PRIVATE Abc::Core)
add_test(NAME abc_ui_helpers COMMAND abc_ui_helpers)

add_executable(abc_ui_registry WeaponRegistry.cpp "${PROJECT_SOURCE_DIR}/src/data/WeaponResource.cpp")
target_include_directories(abc_ui_registry PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_registry PRIVATE Abc::Core)
add_test(NAME abc_ui_registry COMMAND abc_ui_registry)

# T03: Ui module lifecycle seam. Compiles the production registry; fakes
# record the dispatch log (no copies of the registry logic).
add_executable(abc_ui_modules ModuleRegistry.cpp "${PROJECT_SOURCE_DIR}/src/modules/UiModuleRegistry.cpp")
target_include_directories(abc_ui_modules PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_modules PRIVATE Abc::Core)
add_test(NAME abc_ui_modules COMMAND abc_ui_modules)

# Compile the production shutdown entry points, with external services faked.
# Reconfigure on spine edits so the regression cannot silently test stale code.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/rmlui/RmlUiModule.cpp")
file(READ "${PROJECT_SOURCE_DIR}/src/rmlui/RmlUiModule.cpp" ABC_UI_RMLUI_SOURCE)
foreach(ENTRY SHUTDOWN HUD_SHUTDOWN)
    if(ENTRY STREQUAL "SHUTDOWN")
        set(ENTRY_NAME shutdown)
    else()
        set(ENTRY_NAME onHudShutdown)
    endif()
    string(FIND "${ABC_UI_RMLUI_SOURCE}" "void ${ENTRY_NAME}() {" ENTRY_START)
    if(ENTRY_START LESS 0)
        message(FATAL_ERROR "Missing production ${ENTRY_NAME} entry point")
    endif()
    string(SUBSTRING "${ABC_UI_RMLUI_SOURCE}" ${ENTRY_START} -1 ENTRY_TAIL)
    string(FIND "${ENTRY_TAIL}" "\n}" ENTRY_END)
    math(EXPR ENTRY_LENGTH "${ENTRY_END} + 2")
    string(SUBSTRING "${ENTRY_TAIL}" 0 ${ENTRY_LENGTH} ABC_UI_${ENTRY})
endforeach()
configure_file(RmlUiShutdown.cpp.in RmlUiShutdown.cpp @ONLY)
add_executable(abc_ui_shutdown "${CMAKE_CURRENT_BINARY_DIR}/RmlUiShutdown.cpp"
    "${PROJECT_SOURCE_DIR}/src/modules/UiModuleRegistry.cpp")
target_include_directories(abc_ui_shutdown PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_shutdown PRIVATE Abc::Core)
add_test(NAME abc_ui_shutdown COMMAND abc_ui_shutdown)

# T02.a: hook-contract tests. Compiles the real VGUI2Extension dispatcher and
# drives it through proxy harnesses mirroring the shipped wrapper bodies.
set(VGUI2EXTENSION_SRC_DIR "${PROJECT_SOURCE_DIR}/thirdparty/VGUI2Extension")
add_executable(abc_ui_hook_contract HookContract.cpp
    "${VGUI2EXTENSION_SRC_DIR}/src/VGUI2ExtensionInternal.cpp"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp")
target_include_directories(abc_ui_hook_contract PRIVATE
    "${VGUI2EXTENSION_SRC_DIR}/src"
    "${VGUI2EXTENSION_SRC_DIR}/include/Interface"
    "${VGUI2EXTENSION_SRC_DIR}/include/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}"
    "${METAHOOK_INCLUDE_DIR}/Interface"
    "${METAHOOK_INCLUDE_DIR}/Interface/VGUI"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/cl_dll"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/engine"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/pm_shared"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/public"
    "${METAHOOK_INCLUDE_DIR}/VGUI"
    "${METAHOOK_INCLUDE_DIR}/vgui_controls"
    "${METAHOOK_INCLUDE_DIR}/SourceSDK"
    "${STEAMSDK_DIR}")
target_compile_definitions(abc_ui_hook_contract PRIVATE WIN32 _MBCS STEAM_API_NODLL)
target_compile_options(abc_ui_hook_contract PRIVATE /wd4996)
add_test(NAME abc_ui_hook_contract COMMAND abc_ui_hook_contract)

# T04: menu state machine acceptance. Compiles the production controller
# translation unit; the recording backend fakes the GameUiBridge seam.
add_executable(abc_ui_menu_state MenuStateMachine.cpp
    "${PROJECT_SOURCE_DIR}/src/modules/gameui/MenuController.cpp")
target_include_directories(abc_ui_menu_state PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME abc_ui_menu_state COMMAND abc_ui_menu_state)

# T06: input-ownership router acceptance. Links the production router
# translation unit (no model copy); the Rml seam is a recording sink and
# the owners are configurable fakes. Engine-header-free by design.
add_executable(abc_ui_input_router UiInputRouter.cpp
    "${PROJECT_SOURCE_DIR}/src/framework/UiInputRouter.cpp")
target_include_directories(abc_ui_input_router PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_test(NAME abc_ui_input_router COMMAND abc_ui_input_router)

# T05: interactive-document owner acceptance. Links the real
# InteractiveDocument/MainMenuPage translation units against static RmlUi
# with a no-op renderer; the page case loads the shipped abcui assets (ctest
# runs from the assets directory), so the load also validates the RML/RCSS.
add_executable(abc_ui_interactive InteractiveDocument.cpp
    "${PROJECT_SOURCE_DIR}/src/framework/InteractiveDocument.cpp"
    "${PROJECT_SOURCE_DIR}/src/framework/Localization.cpp"
    "${PROJECT_SOURCE_DIR}/src/panels/mainmenu/MainMenuPage.cpp")
target_include_directories(abc_ui_interactive PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${METAHOOK_INCLUDE_DIR}"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/engine"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/cl_dll"
    "${METAHOOK_INCLUDE_DIR}/HLSDK/public")
target_link_libraries(abc_ui_interactive PRIVATE RmlUi::RmlUi Abc::Core)
add_test(NAME abc_ui_interactive COMMAND abc_ui_interactive)
set_tests_properties(abc_ui_interactive PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/assets")
