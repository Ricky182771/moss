# La versión de la app (cmake -P, sin red): VERSION es la única fuente.
#
#   -DSOURCE_DIR=<raíz del repositorio>
#   -DEXECUTABLES=<ejecutable>|<ejecutable>... (opcional)
#
# - VERSION tiene una sola línea X.Y.Z.
# - gui/pubspec.yaml dice la misma (sin el +N de compilación de Flutter); la
#   GUI la manda en initialize (kClientVersion, que gui/test/apply_test.dart
#   compara con pubspec.yaml).
# - Cada ejecutable, con --version, sale con 0 e imprime "<nombre> X.Y.Z" y
#   nada más (ni en stderr: moss-rpc no avisa de stdin). Corre con HOME y
#   XDG_CONFIG_HOME en una carpeta con config.json roto: no la lee.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "Falta -DSOURCE_DIR=...")
endif()

file(READ "${SOURCE_DIR}/VERSION" version_file)
if(NOT version_file MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)\n?$")
    message(FATAL_ERROR "VERSION debe ser una línea X.Y.Z; dice: '${version_file}'")
endif()
set(version "${CMAKE_MATCH_1}")

file(STRINGS "${SOURCE_DIR}/gui/pubspec.yaml" pubspec REGEX "^version:")
if(NOT pubspec MATCHES "^version: *([0-9]+\\.[0-9]+\\.[0-9]+)(\\+[0-9]+)? *$")
    message(FATAL_ERROR "gui/pubspec.yaml sin 'version: X.Y.Z[+N]': '${pubspec}'")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL version)
    message(FATAL_ERROR
        "gui/pubspec.yaml dice ${CMAKE_MATCH_1} y VERSION dice ${version}: súbelas juntas "
        "(y kClientVersion de gui/lib/rpc/client.dart)")
endif()
message(STATUS "versión ${version}: VERSION y gui/pubspec.yaml coinciden")

if(DEFINED EXECUTABLES AND NOT EXECUTABLES STREQUAL "")
    get_filename_component(work "${CMAKE_CURRENT_BINARY_DIR}/version_home" ABSOLUTE)
    file(REMOVE_RECURSE "${work}")
    file(WRITE "${work}/moss/config.json" "{ roto")
    string(REPLACE "|" ";" executables "${EXECUTABLES}")
    foreach(executable IN LISTS executables)
        get_filename_component(name "${executable}" NAME_WE)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env HOME=${work} XDG_CONFIG_HOME=${work}
                APPDATA=${work} "${executable}" --version
            INPUT_FILE "${work}/moss/config.json"
            RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
        string(REPLACE "\r" "" out "${out}")
        if(NOT code EQUAL 0 OR NOT out STREQUAL "${name} ${version}\n" OR NOT err STREQUAL "")
            message(FATAL_ERROR "${name} --version: código ${code}, salida '${out}', "
                                "errores '${err}'; se esperaba '${name} ${version}'")
        endif()
        message(STATUS "${name} --version: ${name} ${version}")
    endforeach()
endif()
