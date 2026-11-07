/// Buscar chats por título (DISENO.md → Buscar chats), sin distinguir
/// mayúsculas ni acentos: "cancion" encuentra "Canción".
library;

const _plain = {
  'á': 'a', 'à': 'a', 'â': 'a', 'ä': 'a', 'ã': 'a', 'å': 'a', 'ā': 'a', //
  'é': 'e', 'è': 'e', 'ê': 'e', 'ë': 'e', 'ē': 'e', //
  'í': 'i', 'ì': 'i', 'î': 'i', 'ï': 'i', 'ī': 'i', //
  'ó': 'o', 'ò': 'o', 'ô': 'o', 'ö': 'o', 'õ': 'o', 'ø': 'o', 'ō': 'o', //
  'ú': 'u', 'ù': 'u', 'û': 'u', 'ü': 'u', 'ū': 'u', //
  'ñ': 'n', 'ç': 'c', 'ý': 'y', 'ÿ': 'y',
};

/// Una marca combinante (U+0300–U+036F): el acento escrito aparte.
bool _combining(int unit) => unit >= 0x300 && unit <= 0x36F;

/// [text] en minúsculas y sin acentos, y para cada unidad del resultado, el
/// índice de [text] de donde salió (para resaltar la coincidencia).
(String, List<int>) _fold(String text) {
  final out = StringBuffer();
  final from = <int>[];
  for (var i = 0; i < text.length; i++) {
    final unit = text.codeUnitAt(i);
    if (_combining(unit)) continue;
    final lower = String.fromCharCode(unit).toLowerCase();
    final plain = _plain[lower] ?? lower;
    out.write(plain);
    for (var k = 0; k < plain.length; k++) {
      from.add(i);
    }
  }
  return (out.toString(), from);
}

/// Dónde está [query] en [title] (`[inicio, fin)` en índices de [title]), o
/// null si no está. Una consulta vacía coincide sin resaltar nada: `(0, 0)`.
(int, int)? findInTitle(String title, String query) {
  final (q, _) = _fold(query.trim());
  if (q.isEmpty) return (0, 0);
  final (t, from) = _fold(title);
  final at = t.indexOf(q);
  if (at < 0) return null;
  var end = from[at + q.length - 1] + 1;
  // El acento escrito aparte va con su letra.
  while (end < title.length && _combining(title.codeUnitAt(end))) {
    end++;
  }
  return (from[at], end);
}
