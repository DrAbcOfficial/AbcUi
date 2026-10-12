add_executable(abc_ui_helpers UiHelpers.cpp)
target_include_directories(abc_ui_helpers PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_helpers PRIVATE Abc::Core)
add_test(NAME abc_ui_helpers COMMAND abc_ui_helpers)

add_executable(abc_ui_registry WeaponRegistry.cpp "${PROJECT_SOURCE_DIR}/src/data/WeaponResource.cpp")
target_include_directories(abc_ui_registry PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_registry PRIVATE Abc::Core)
add_test(NAME abc_ui_registry COMMAND abc_ui_registry)

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
