# Shared build conventions for ABC projects.
# NOTE: keep this file in sync across AbcCore / AbcEssential / AbcUi repositories.

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P STREQUAL "4")
    message(FATAL_ERROR "ABC projects require Windows MSVC x86. Configure with: cmake -A Win32 ...")
endif()

# Dynamic CRT (/MD, /MDd) - AbcCore.dll and the plugin DLLs exchange STL
# objects, so every ABC module must share one runtime heap. Keep this before
# add_subdirectory'ed third-party projects so they use the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All sources are UTF-8. Code ported from ABCEnchance must be converted from GBK first.
add_compile_options(
    /utf-8
    /MP
    /wd4996 # deprecated CRT (GoldSrc SDK style)
    /wd4311 # pointer truncation (32-bit engine pointers)
    /wd4312 # pointer conversion
    /wd4819 # source charset (third-party headers)
    /wd4828 # source charset (third-party headers, HLSDK)
)
