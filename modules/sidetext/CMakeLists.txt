# abcui.sidetext - side score/speed readout module (see sdk/abcui/abi.h for
# the module build contract).
abcui_add_module(sidetext
    SOURCES
        src/SideTextModule.cpp
    LIBS
        gdi32)
