cmake_minimum_required(VERSION 3.24)

project(AbcUi LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AbcCommon.cmake")

set(ABC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/AbcCore")
add_subdirectory("${ABC_CORE_DIR}" AbcCore-build)

set(METAHOOK_INCLUDE_DIR "${ABC_CORE_DIR}/thirdparty/MetaHook/include")
set(VGUI2EXTENSION_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/VGUI2Extension/include")

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

add_library(AbcUi MODULE
    src/plugins.cpp
    src/hooks.cpp
    src/rmlui/AbcRenderInterface.cpp
    src/rmlui/AbcSystemInterface.cpp
    src/rmlui/InputBridge.cpp
    src/rmlui/KeyMap.cpp
    src/rmlui/RmlUiModule.cpp
    src/rmlui/SourceSpewStub.cpp
    src/rmlui/VGUI2ExtensionImport.cpp
    src/framework/Assets.cpp
    src/framework/HudPanel.cpp
    src/framework/Localization.cpp
    src/data/NetworkMessages.cpp
    src/data/SpriteResource.cpp
    src/framework/PanelManager.cpp
    src/panels/demo/DemoPanel.cpp
    # Generic CreateInterface export (EXPOSE_SINGLE_INTERFACE implementation)
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
)
target_compile_definitions(AbcUi PRIVATE WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS)
target_link_libraries(AbcUi PRIVATE Abc::Core RmlUi::RmlUi FreeImage)
# Include layout mirrors Renderer's renderer_settings interface set: the
# VGUI2Extension interface headers include MetaHook VGUI/SourceSDK headers by
# bare name (<VGUI.h>, <Color.h>, <mathlib/vector2d.h>).
target_include_directories(AbcUi PRIVATE
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
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/abcui")
        install(DIRECTORY assets/abcui DESTINATION "${ABC_GAME_DIR}/svencoop"
            PATTERN ".gitkeep" EXCLUDE)
    endif()
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        COMMENT "Deploying AbcUi.dll, FreeImage.dll and assets to ${ABC_GAME_DIR}")
endif()
