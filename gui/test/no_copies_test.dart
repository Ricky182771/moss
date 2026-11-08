import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

/// Lo que R6 quitó de Dart porque ahora lo da el protocolo (guias/16-gui.md):
/// nada de esto debe volver a `lib/`.
const _gone = {
  // El conteo del Pensamiento: `reasoning_words`.
  'thoughtSummary(String': 'la función',
  r"RegExp(r'[^ \n\t\r]+')": 'su expresión',
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
