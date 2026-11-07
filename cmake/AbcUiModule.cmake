# First-party UI module build contract: one MODULE DLL per modules/<id>, the
# abcui_<id> output name, the SDK include path and the shared compile flags.
# Keep the packaging contract (entry DLL + manifest + assets) in pack.py; this
# helper only removes the per-directory boilerplate.

function(abcui_add_module id)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;LIBS;OPTIONS" ${ARGN})
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "abcui_add_module(${id}): no SOURCES")
  endif()
  set(target "abcui_mod_${id}")
  add_library(${target} MODULE ${ARG_SOURCES})
  target_include_directories(${target} PRIVATE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../sdk"
      ${ARG_INCLUDES})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "abcui_${id}")
  target_compile_definitions(${target} PRIVATE
                             WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
                             NOMINMAX)
  target_compile_options(${target} PRIVATE /W4 ${ARG_OPTIONS})
  if(ARG_LIBS)
    target_link_libraries(${target} PRIVATE ${ARG_LIBS})
  endif()
endfunction()
