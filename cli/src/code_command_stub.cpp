// Compilación sin Moss Code (CHATBOT_BUILD_CODE=OFF): CMake elige este archivo en lugar de
// code_command.cpp.

#include "code_command.h"

#include <iostream>

namespace chatbot::cli {

int run_code_command(const CommandLine&) {
    std::cerr << "Moss Code no está incluido en esta compilación.\n";
    return kExitUnavailable;
}

int run_code_screen(const std::string&) {
    return run_code_command({});
}

} // namespace chatbot::cli
