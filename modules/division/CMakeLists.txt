# abcui.division - the third-person HUD group (see sdk/abcui/abi.h for the
# module build contract).
add_library(abcui_mod_division MODULE src/DivisionModule.cpp)
target_include_directories(abcui_mod_division PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
set_target_properties(abcui_mod_division PROPERTIES OUTPUT_NAME "abcui_division")
target_compile_definitions(abcui_mod_division PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_division PRIVATE /W4)
