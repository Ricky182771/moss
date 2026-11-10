# La versión de la app (cmake -P, sin red): VERSION es la única fuente.
#
#   -DSOURCE_DIR=<raíz del repositorio>
#
# - VERSION tiene una sola línea X.Y.Z.
# - gui/pubspec.yaml dice la misma (sin el +N de compilación de Flutter); la
#   GUI la manda en initialize (kClientVersion, que gui/test/apply_test.dart
#   compara con pubspec.yaml).

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
