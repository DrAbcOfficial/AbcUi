# abcui.ammo - ammo readout and pickup stacks module (see sdk/abcui/abi.h for
# the module build contract).
abcui_add_module(ammo
    SOURCES
        src/AmmoModule.cpp
    LIBS
        gdi32)
