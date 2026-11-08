import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

/// Lo que R6 quitó de Dart porque ahora lo da el protocolo (guias/16-gui.md):
/// nada de esto debe volver a `lib/`.
const _gone = {
  // El conteo del Pensamiento: `reasoning_words`.
  'thoughtSummary(String': 'la función',
  r"RegExp(r'[^ \n\t\r]+')": 'su expresión',
  // Los textos y la lectura de números: `settings.model_params.*`.
  'reasoningHelp(String': 'la ayuda por estilo',
  'static const topKNote': 'la nota de Top K',
  'static const extraHelp': 'la nota del JSON',
  'notANumber': 'el error de un número',
  'notAnInteger': 'el error de un entero',
  'num.parse': 'leer un número',
  r"RegExp(r'^-?\d+": 'decidir qué es un número',
};

void main() {
  test('gui/lib no tiene las copias de la TUI que se quitaron', () {
    final found = <String>[];
    for (final file in Directory('lib').listSync(recursive: true)) {
      if (file is! File || !file.path.endsWith('.dart')) continue;
      final text = file.readAsStringSync();
      for (final gone in _gone.keys) {
        if (text.contains(gone)) found.add('${file.path}: $gone');
      }
    }
    expect(found, isEmpty);
  });
}
