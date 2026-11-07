# abcui.demo - package-load smoke module (see sdk/abcui/abi.h for the module
# build contract). Development target only: the pack tool zips the DLL with
# manifest.json + assets; the demo package never ships in dist.
abcui_add_module(demo
    SOURCES
        src/DemoModule.cpp)
