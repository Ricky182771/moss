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

# Instrucciones fijas del modo código: src/system_prompt.md, embebido como cadena cruda en un .cpp
# generado. Un cambio en el .md vuelve a configurar y recompila.
set(CHATBOT_CODE_PROMPT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/system_prompt.md)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CHATBOT_CODE_PROMPT_FILE})
file(READ ${CHATBOT_CODE_PROMPT_FILE} CHATBOT_CODE_SYSTEM_PROMPT)
# Sin \r: un checkout con CRLF (Windows) da el mismo texto.
string(REPLACE "\r" "" CHATBOT_CODE_SYSTEM_PROMPT "${CHATBOT_CODE_SYSTEM_PROMPT}")
string(FIND "${CHATBOT_CODE_SYSTEM_PROMPT}" ")moss_prompt\"" CHATBOT_CODE_PROMPT_DELIMITER)
if(NOT CHATBOT_CODE_PROMPT_DELIMITER EQUAL -1)
    message(FATAL_ERROR "system_prompt.md no puede contener )moss_prompt\" (cierra la cadena cruda)")
endif()
# 65536 caracteres: el mínimo que el estándar garantiza para un literal.
string(LENGTH "${CHATBOT_CODE_SYSTEM_PROMPT}" CHATBOT_CODE_PROMPT_LENGTH)
if(CHATBOT_CODE_PROMPT_LENGTH GREATER 60000)
    message(FATAL_ERROR "system_prompt.md mide más de 60000 bytes: hay que partirlo en trozos")
endif()
configure_file(src/system_prompt_text.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/system_prompt_text.cpp @ONLY)

add_library(chatbot_code STATIC
    ${CHATBOT_CODE_PLATFORM_SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/system_prompt_text.cpp
    src/system_prompt.cpp
    src/agent_loop.cpp
    src/code_limits.cpp
    src/code_session.cpp
    src/permissions.cpp
    src/output_cap.cpp
    src/project_root.cpp
    src/glob_match.cpp
    src/glob_tool.cpp
    src/grep_tool.cpp
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
