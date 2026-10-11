add_executable(abc_ui_helpers UiHelpers.cpp)
target_include_directories(abc_ui_helpers PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(abc_ui_helpers PRIVATE Abc::Core)
add_test(NAME abc_ui_helpers COMMAND abc_ui_helpers)
