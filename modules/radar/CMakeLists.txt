# abcui.radar - minimap plate module (see sdk/abcui/abi.h for the module build
# contract). The world-to-FBO producer stays in the framework
# (src/render/RadarRender.cpp).
abcui_add_module(radar
    SOURCES
        src/RadarModule.cpp)
