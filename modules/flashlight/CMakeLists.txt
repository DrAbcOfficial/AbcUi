# abcui.flashlight - flashlight icon/battery readout module (see
# sdk/abcui/abi.h for the module build contract).
abcui_add_module(flashlight
    SOURCES
        src/FlashlightModule.cpp
    LIBS
        gdi32)
