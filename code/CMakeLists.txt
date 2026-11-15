# Motor de Moss Code (guias/18-moss-code.md): herramientas, permisos y bucle del
# agente, sin terminal ni FTXUI. Depende del núcleo y de chatbot::process; nunca
# de cli/, FTXUI ni addons. Se compila con CHATBOT_BUILD_CODE (ON por defecto).
find_package(nlohmann_json 3.9 REQUIRED)

# Rutas del proyecto (project_root.h): CMake elige la parte de cada sistema.
if(WIN32)
    set(CHATBOT_CODE_PLATFORM_SOURCE src/project_root_windows.cpp)
else()
    set(CHATBOT_CODE_PLATFORM_SOURCE src/project_root_posix.cpp)
endif()

add_library(chatbot_code STATIC
    ${CHATBOT_CODE_PLATFORM_SOURCE}
    src/output_cap.cpp
    src/project_root.cpp
    src/glob_match.cpp
    src/glob_tool.cpp
    src/read_tool.cpp
    src/tool_registry.cpp
    src/todo_write_tool.cpp
    src/tool_schema.cpp)
add_library(chatbot::code ALIAS chatbot_code)
target_compile_features(chatbot_code PUBLIC cxx_std_20)
target_include_directories(chatbot_code PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(chatbot_code
    # process es pública: tool.h expone ProcessLauncher (la forma de run_process).
    PUBLIC chatbot::core chatbot::process
    PRIVATE nlohmann_json::nlohmann_json)
chatbot_set_warnings(chatbot_code)

if(CHATBOT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
