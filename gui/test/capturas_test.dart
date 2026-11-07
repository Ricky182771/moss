@Tags(['capturas'])
library;

import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/screens/error_screen.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/theme/theme.dart';

import 'fake_server.dart';
import 'harness.dart';

/// PNG de 1440×900 en oscuro y claro, para compararlos con las maquetas.
const _out = '/tmp/moss-capturas';
const _size = Size(1440, 900);
final _key = GlobalKey();

Future<void> _font(String family, List<String> files) async {
  final loader = FontLoader(family);
  for (final f in files) {
    loader.addFont(
      Future.value(ByteData.sublistView(File(f).readAsBytesSync())),
    );
  }
  await loader.load();
}

Future<void> _shot(WidgetTester tester, String name) async {
  await tester.runAsync(() async {
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(_key),
    );
    final image = await boundary.toImage();
    final png = await image.toByteData(format: ui.ImageByteFormat.png);
    File('$_out/$name.png').writeAsBytesSync(png!.buffer.asUint8List());
  });
}

final _summaries = [
  summary('a', 'Selection sort en C++', group: 'Últimos 7 días'),
  summary('b', 'Fotosíntesis en dos líneas', group: 'Últimos 7 días'),
  summary('c', 'Resuelve x² − 5x + 6 = 0', group: '18 oct'),
  summary(
    'd',
    'Noticias recientes de Linux',
    group: '10 oct',
    usedSearch: true,
  ),
  summary('e', '¿Por qué Python es tan popular?', group: '10 oct'),
  summary('f', 'Calculadora en C++', group: '8 oct'),
  summary(
    'g',
    '¿Quién ganó el último partido?',
    group: '8 oct',
    usedSearch: true,
  ),
];

const _code = '''Busca el elemento más chico de lo que falta por ordenar y lo pone al frente. Repite hasta terminar.

```cpp
void selection_sort(std::vector<int>& v) {
    for (size_t i = 0; i + 1 < v.size(); ++i) {
        size_t min = i;
        for (size_t j = i + 1; j < v.size(); ++j)
            if (v[j] < v[min]) min = j;
        std::swap(v[i], v[min]);
    }
}
```

- El ciclo de afuera marca dónde empieza la parte sin ordenar.
- El de adentro busca el mínimo de esa parte.
- `std::swap` lo pone en su lugar.''';

final _chat = snapshot(
  rev: 4,
  id: 'a',
  title: 'Selection sort en C++',
  phase: 'streaming',
  entries: [
    entry('user', text: 'Dame un selection sort en C++ y explícamelo corto.'),
    entry(
      'assistant',
      text: _code,
      reasoning: List.filled(52, 'palabra').join(' '),
    ),
    entry('user', text: '¿Y cuál es su complejidad?'),
    entry(
      'assistant',
      inProgress: true,
      reasoning: List.filled(41, 'palabra').join(' '),
      text:
          'Es **O(n²)** en todos los casos: siempre recorre lo que falta para '
          'encontrar el mínimo, aunque el arreglo ya venga ordenado',
    ),
  ],
);

final _thinking = snapshot(
  rev: 3,
  id: 'a',
  title: 'Selection sort en C++',
  phase: 'thinking',
  entries: [
    entry('user', text: '¿Y cuál es su complejidad?'),
    entry(
      'assistant',
      inProgress: true,
      reasoning:
          'Preguntan la complejidad. Selection sort siempre recorre lo que '
          'falta para hallar el mínimo, sin importar el orden inicial: '
          'n−1 + n−2 + … + 1 comparaciones, o sea n(n−1)/2.\n\n'
          'Es O(n²) en el mejor, el promedio y el peor caso. Conviene decirlo '
          'en una frase y explicar por qué no mejora con datos ordenados.',
    ),
  ],
);

void main() {
  setUpAll(() async {
    Directory(_out).createSync(recursive: true);
    await _font('Geist', [
      'assets/fonts/Geist-Regular.otf',
      'assets/fonts/Geist-Medium.otf',
      'assets/fonts/Geist-SemiBold.otf',
    ]);
    await _font('Geist Mono', [
      'assets/fonts/GeistMono-Regular.otf',
      'assets/fonts/GeistMono-Medium.otf',
    ]);
    // flutter_tester está en <flutter>/bin/cache/artifacts/engine/<plataforma>/.
    final artifacts = File(Platform.resolvedExecutable).parent.parent.parent;
    await _font('MaterialIcons', [
      '${artifacts.path}/material_fonts/MaterialIcons-Regular.otf',
    ]);
  });

  for (final (mode, theme) in [
    ('oscuro', MossTheme.dark()),
    ('claro', MossTheme.light()),
  ]) {
    testWidgets('chat con respuesta y código ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        conversation: _chat,
        summaries: _summaries,
      );
      await tester.pump(const Duration(milliseconds: 100));
      await _shot(tester, 'chat-$mode');
    });

    testWidgets('pensamiento llegando ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        conversation: _thinking,
        summaries: _summaries,
      );
      await tester.pump(const Duration(milliseconds: 100));
      await _shot(tester, 'pensamiento-$mode');
    });

    testWidgets('chat vacío ($mode)', (tester) async {
      await startApp(tester, theme: theme, key: _key, summaries: _summaries);
      await tester.pump(const Duration(milliseconds: 100));
      await _shot(tester, 'vacio-$mode');
    });

    testWidgets('ventana angosta ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        size: const Size(860, 900),
        conversation: _chat,
        summaries: _summaries,
      );
      await tester.pump(const Duration(milliseconds: 100));
      await _shot(tester, 'angosta-$mode');
    });

    testWidgets('error de moss-rpc ($mode)', (tester) async {
      tester.view.physicalSize = _size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        RepaintBoundary(
          key: _key,
          child: MaterialApp(
            theme: theme,
            debugShowCheckedModeBanner: false,
            home: const RpcErrorScreen(
              title: Strings.rpcCannotStart,
              detail: 'config.json no es JSON válido: línea 3, columna 5.',
              path: '/opt/moss/moss-rpc',
              stderr: [
                'moss-rpc: leyendo /home/r/.config/moss/config.json',
                'moss-rpc: config.json no es JSON válido: línea 3, columna 5.',
              ],
            ),
          ),
        ),
      );
      await _shot(tester, 'error-$mode');
    });
  }
}
