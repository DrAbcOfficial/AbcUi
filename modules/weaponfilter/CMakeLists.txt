# abcui.weaponfilter - client-side weapon selector layout rules (see
# sdk/abcui/abi.h for the module build contract). The rule model parses its
# config section with header-only rapidjson, so the module needs a third-party
# include path but never a framework link.
add_library(abcui_mod_weaponfilter MODULE src/WeaponFilterModule.cpp
                                          src/WeaponFilterModel.cpp)
target_include_directories(abcui_mod_weaponfilter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk"
    "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
set_target_properties(abcui_mod_weaponfilter PROPERTIES OUTPUT_NAME
                                                        "abcui_weaponfilter")
target_compile_definitions(abcui_mod_weaponfilter PRIVATE
                           WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                           NOMINMAX)
target_compile_options(abcui_mod_weaponfilter PRIVATE /W4)
