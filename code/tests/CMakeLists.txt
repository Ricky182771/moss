# Pruebas de chatbot_code, sin red. Usan los helpers de tests/.
add_executable(chatbot_code_tests
    test_glob_match.cpp
    test_glob_tool.cpp
    test_grep_tool.cpp
    test_output_cap.cpp
    test_project_root.cpp
    test_read_tool.cpp
    test_tool_registry.cpp
    test_todo_write_tool.cpp
    test_tool_schema.cpp)
# Uniones y enlaces simbólicos creados con Win32: solo en Windows.
if(WIN32)
    target_sources(chatbot_code_tests PRIVATE test_project_root_windows.cpp)
endif()
target_compile_features(chatbot_code_tests PRIVATE cxx_std_20)
# code/src: el validador y las herramientas (tool_schema.h, tool_internal.h) usan nlohmann.
target_include_directories(chatbot_code_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/code/src)
target_link_libraries(chatbot_code_tests PRIVATE
    chatbot::code Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
chatbot_set_warnings(chatbot_code_tests)
chatbot_windows_manifest(chatbot_code_tests)
add_test(NAME chatbot_code_tests COMMAND chatbot_code_tests)
