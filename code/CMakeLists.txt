# Motor de Moss Code (guias/18-moss-code.md): herramientas, permisos y bucle del
# agente, sin terminal ni FTXUI. Depende del núcleo y de chatbot::process; nunca
# de cli/, FTXUI ni addons. Se compila con CHATBOT_BUILD_CODE (ON por defecto).
find_package(nlohmann_json 3.9 REQUIRED)

add_library(chatbot_code STATIC
    src/output_cap.cpp)
add_library(chatbot::code ALIAS chatbot_code)
target_compile_features(chatbot_code PUBLIC cxx_std_20)
target_include_directories(chatbot_code PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(chatbot_code
    PUBLIC chatbot::core
    PRIVATE chatbot::process nlohmann_json::nlohmann_json)
chatbot_set_warnings(chatbot_code)

if(CHATBOT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
