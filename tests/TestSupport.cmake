# Unified CTest target helper for the offline suite.
#
# Every test is one executable, runs from the repository asset root, links the
# shared harness, and is registered under the same conventions:
#   - test names are the executable names (abc_ui_*);
#   - WORKING_DIRECTORY is <repo>/assets, so shipped RML/RCSS/JSON paths in the
#     tests stay repository-relative instead of machine-relative;
#   - no test reads environment variables or the game installation.
include_guard(GLOBAL)

function(abcui_add_test name)
  cmake_parse_arguments(ARG "VGUI" "" "SOURCES;LIBRARIES;DEFINITIONS;OPTIONS;INCLUDE_DIRS" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "abcui_add_test(${name}): unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "abcui_add_test(${name}): no sources")
  endif()

  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE abcui_test_support ${ARG_LIBRARIES})
  target_include_directories(${name} PRIVATE
    ${ABCUI_TEST_INCLUDE_DIRS}
    ${ARG_INCLUDE_DIRS})
  if(ARG_DEFINITIONS)
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINITIONS})
  endif()
  if(ARG_OPTIONS)
    target_compile_options(${name} PRIVATE ${ARG_OPTIONS})
  endif()

  if(ARG_VGUI)
    target_include_directories(${name} PRIVATE ${ABCUI_TEST_VGUI_INCLUDE_DIRS})
    target_compile_definitions(${name} PRIVATE WIN32 _MBCS STEAM_API_NODLL)
    target_compile_options(${name} PRIVATE /wd4996)
  endif()

  add_dependencies(${name} abcui_test_runtime)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/assets"
    LABELS "offline"
    TIMEOUT 180)
endfunction()
