add_executable(smoke smoke.cpp)

target_compile_features(smoke PRIVATE cxx_std_20)
target_link_libraries(smoke PRIVATE chatbot::core)

chatbot_set_warnings(smoke)
chatbot_windows_manifest(smoke)

# Sonda de tool calling para elegir modelos (a mano; sin install ni paquetes).
find_package(nlohmann_json 3.9 REQUIRED)
add_executable(moss-tools-probe tools_probe.cpp)
target_compile_features(moss-tools-probe PRIVATE cxx_std_20)
target_link_libraries(moss-tools-probe PRIVATE chatbot::core nlohmann_json::nlohmann_json)
chatbot_set_warnings(moss-tools-probe)
chatbot_windows_manifest(moss-tools-probe)

# Vista previa del render de markdown (sin red ni API).
add_executable(md_preview md_preview.cpp)
target_compile_features(md_preview PRIVATE cxx_std_20)
target_link_libraries(md_preview PRIVATE chatbot_cli_ui)
chatbot_set_warnings(md_preview)
chatbot_windows_manifest(md_preview)
