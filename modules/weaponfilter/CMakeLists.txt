# abcui.weaponfilter - client-side weapon selector layout rules (see
# sdk/abcui/abi.h for the module build contract). The rule model parses its
# config section with header-only rapidjson, so the module needs a third-party
# include path but never a framework link.
abcui_add_module(weaponfilter
    SOURCES
        src/WeaponFilterModule.cpp
        src/WeaponFilterModel.cpp
    INCLUDES
        "${ABC_CORE_DIR}/thirdparty/MetaHook/thirdparty/rapidjson/include")
