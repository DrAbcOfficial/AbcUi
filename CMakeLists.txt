cmake_minimum_required(VERSION 3.24)

project(AbcUi LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AbcCommon.cmake")

set(ABC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/AbcCore")
add_subdirectory("${ABC_CORE_DIR}" AbcCore-build)

set(METAHOOK_INCLUDE_DIR "${ABC_CORE_DIR}/thirdparty/MetaHook/include")

# NOTE (Phase 0): RmlUi / freetype / FreeImage submodules are pinned under
# thirdparty/ but intentionally not wired into the build yet. RmlUi
# integration (RenderInterface over IMetaRenderer, font engine, image
# decoding) lands in Phase 3 - see plans_docs/05-abcui.md.

add_library(AbcUi MODULE
    src/plugins.cpp
    # Generic CreateInterface export (EXPOSE_SINGLE_INTERFACE implementation)
    "${METAHOOK_INCLUDE_DIR}/HLSDK/common/interface.cpp"
)
target_compile_definitions(AbcUi PRIVATE WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS)
target_link_libraries(AbcUi PRIVATE Abc::Core)

if(ABC_GAME_DIR)
    install(TARGETS AbcUi
        RUNTIME DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins"
        LIBRARY DESTINATION "${ABC_GAME_DIR}/svencoop/metahook/plugins")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/abcui")
        install(DIRECTORY assets/abcui DESTINATION "${ABC_GAME_DIR}/svencoop"
            PATTERN ".gitkeep" EXCLUDE)
    endif()
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        COMMENT "Deploying AbcUi.dll and assets to ${ABC_GAME_DIR}")
endif()
