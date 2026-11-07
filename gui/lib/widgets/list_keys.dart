import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// ↑ ↓ y Enter de una lista con una fila resaltada, mientras el foco se queda
/// en un campo de texto (Opciones, Buscar chats, menú de `/`). Salta las
/// filas que [enabled] no deja elegir.
KeyEventResult listKeys(
  KeyEvent e, {
  required int count,
  required int index,
  required ValueChanged<int> move,
  required VoidCallback? activate,
  bool Function(int i)? enabled,
}) {
  if (e is KeyUpEvent) return KeyEventResult.ignored;
  final key = e.logicalKey;
  final step = key == LogicalKeyboardKey.arrowDown
      ? 1
      : key == LogicalKeyboardKey.arrowUp
      ? -1
      : 0;
  if (step != 0) {
    for (var i = index + step; i >= 0 && i < count; i += step) {
      if (enabled?.call(i) ?? true) {
        move(i);
        break;
      }
    }
    return KeyEventResult.handled; // Aunque no se mueva: no al campo.
  }
  final enter =
      key == LogicalKeyboardKey.enter || key == LogicalKeyboardKey.numpadEnter;
  if (enter && e is KeyDownEvent && activate != null) {
    activate();
    return KeyEventResult.handled;
  }
  return KeyEventResult.ignored;
}

/// La primera fila que se puede elegir, o -1.
int firstEnabled(int count, bool Function(int i)? enabled) {
  for (var i = 0; i < count; i++) {
    if (enabled?.call(i) ?? true) return i;
  }
  return -1;
}

/// Desplaza [scroll] lo justo para que se vea la fila [index] (filas de alto
/// fijo [extent]).
void revealRow(ScrollController scroll, int index, double extent) {
  if (!scroll.hasClients || index < 0) return;
  final p = scroll.position;
  final top = index * extent;
  if (top < p.pixels) {
    scroll.jumpTo(top);
  } else if (top + extent > p.pixels + p.viewportDimension) {
    scroll.jumpTo(
      (top + extent - p.viewportDimension).clamp(0, p.maxScrollExtent),
    );
  }
}
