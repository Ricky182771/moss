# Programas externos sin shell (chatbot/child_process.h): la usan cli/ (portapapeles,
# systemctl, el lanzador de addons) y chatbot_code (git y rg). Solo
# depende del núcleo (CancelToken); sin FTXUI ni addons. CMake elige el .cpp por
# plataforma: el de Windows solo trae lo mínimo para compilar.
if(WIN32)
    set(CHATBOT_PROCESS_SOURCE src/child_process_windows.cpp)
else()
    set(CHATBOT_PROCESS_SOURCE src/child_process_posix.cpp)
endif()

add_library(chatbot_process STATIC ${CHATBOT_PROCESS_SOURCE})
add_library(chatbot::process ALIAS chatbot_process)
target_compile_features(chatbot_process PUBLIC cxx_std_20)
target_include_directories(chatbot_process PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(chatbot_process PUBLIC chatbot::core)
chatbot_set_warnings(chatbot_process)
