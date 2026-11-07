/// Menú de `/` de la caja, como el de la TUI (`cli/src/command_menu.cpp`).
library;

import '../models/settings.dart';

/// Se ve mientras se escribe el nombre: empieza con "/" y aún no hay espacio
/// ni salto de línea.
bool typingCommand(String text) =>
    text.startsWith('/') && !text.contains(RegExp(r'\s'));

/// Los comandos cuyo nombre empieza con [text], sin distinguir mayúsculas,
/// en el orden del servidor.
List<CommandInfo> filterCommands(List<CommandInfo> all, String text) {
  final q = text.toLowerCase();
  return [
    for (final c in all)
      if (c.name.toLowerCase().startsWith(q)) c,
  ];
}

/// Lo que queda en la caja al completar: con espacio si lleva argumentos.
String completeCommand(CommandInfo c) =>
    c.arguments.isEmpty ? c.name : '${c.name} ';
