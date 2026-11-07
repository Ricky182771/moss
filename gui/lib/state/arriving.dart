/// Texto del modelo llegando (DISENO.md → Animaciones): por palabra y, en los
/// bloques de código, por línea. Reglas puras; el dibujo está en
/// `widgets/arriving_text.dart`.
library;

/// Como mucho estas palabras animándose a la vez; una ráfaga más grande
/// aparece directo.
const kMaxAnimatedWords = 40;

final _fence = RegExp(r'^ {0,3}(```|~~~)', multiLine: true);
final _space = RegExp(r'\s');
final _word = RegExp(r'\S+');

/// Cuánto de [text] (que sigue llegando) se muestra: hasta la última palabra
/// completa; dentro de un bloque de código, hasta la última línea completa.
/// Así una palabra cortada por un trozo no sale a medias.
///
/// shortcut: "dentro de un bloque" cuenta las cercas ``` o ~~~ al principio
/// de línea, sin el resto de CommonMark (cercas más largas, sangría en
/// listas). Si falla, solo cambia dónde se corta mientras llega.
int shownLength(String text) {
  if (_fence.allMatches(text).length.isOdd) {
    return text.lastIndexOf('\n') + 1;
  }
  if (text.isEmpty || _space.hasMatch(text[text.length - 1])) {
    return text.length;
  }
  for (var i = text.length - 1; i >= 0; i--) {
    if (_space.hasMatch(text[i])) return i + 1;
  }
  return 0;
}

/// Las palabras de un trozo (para el tope de [kMaxAnimatedWords]).
int countWords(String s) => _word.allMatches(s).length;
