@Tags(['capturas'])
library;

import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/models/gui_prefs.dart';
import 'package:moss_gui/screens/error_screen.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/theme/theme.dart';
import 'package:moss_gui/widgets/settings_form.dart';

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
      reasoningWords: 52,
    ),
    entry('user', text: '¿Y cuál es su complejidad?'),
    entry(
      'assistant',
      inProgress: true,
      reasoning: List.filled(41, 'palabra').join(' '),
      reasoningWords: 41,
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
      reasoningWords: 55,
    ),
  ],
);

/// Lo que piden Opciones, el menú de / y el menú ⋯.
Map<String, Object? Function(Map<String, dynamic>)> _handlers({
  bool loading = false,
}) => {
  'settings.provider.get': (_) => providerSettings(),
  'commands.list': (_) => {
    'commands': [
      {
        'name': '/buscar',
        'arguments': '<consulta>',
        'description': 'Busca en la web y responde con fuentes citadas',
        'needs_argument': true,
        'available': true,
        'note': '',
      },
      {
        'name': '/copiar',
        'arguments': '[n]',
        'description': 'Copia un bloque de código',
        'needs_argument': false,
        'available': true,
        'note': '',
      },
      {
        'name': '/guardar',
        'arguments': '[n]',
        'description': 'Guarda un bloque de código en Descargas',
        'needs_argument': false,
        'available': false,
        'note': 'no hay bloques de código',
      },
      {
        'name': '/exportar',
        'arguments': '',
        'description': 'Exporta la conversación a Markdown',
        'needs_argument': false,
        'available': true,
        'note': '',
      },
      {
        'name': '/model',
        'arguments': '[proveedor] [modelo]',
        'description': 'Cambia de modelo',
        'needs_argument': false,
        'available': true,
        'note': '',
      },
    ],
  },
  'models.list': (_) => loading
      ? noReply
      : {
          'models': [
            'z-ai/glm5',
            'moonshotai/kimi-k3',
            'nvidia/nemotron-3-ultra',
            'qwen/qwen3.5-coder',
            'deepseek-ai/deepseek-v4',
          ],
        },
};

/// Deja pasar los cuadros de una ventana que se abre (y sus respuestas).
Future<void> _settle(WidgetTester tester) async {
  for (var i = 0; i < 5; i++) {
    await tester.pump();
  }
  await tester.pump(const Duration(milliseconds: 400));
}

Future<void> _typeInBox(WidgetTester tester, String text) async {
  await tester.enterText(find.byType(TextField).first, text);
  await _settle(tester);
}

/// El registro de la maqueta `Config-registro`: ayer y hoy, con errores.
List<String> sampleLog() => [
  journalLine(-1, '22:41:07', 'Mensaje de 123456789 respondido en 4.1 s'),
  journalLine(-1, '22:58:30', 'Invitado 555123987 quitado'),
  journalLine(-1, '23:10:02', 'Deteniendo: SIGTERM'),
  journalLine(
    0,
    '08:58:02',
    'Started moss-telegram.service - Moss: bot de Telegram.',
  ),
  journalLine(
    0,
    '08:58:03',
    'Conectado como @moss_prueba_bot · 2 IDs autorizados',
  ),
  journalLine(0, '09:02:11', 'Mensaje de 123456789 respondido en 3.4 s'),
  journalLine(0, '09:05:46', 'Mensaje de 987654321 respondido en 6.0 s'),
  journalLine(
    0,
    '09:06:20',
    'Error al responder a 987654321: HTTP 429 del proveedor (límite de '
        'solicitudes)',
  ),
  journalLine(
    0,
    '09:20:33',
    'Error de red con Telegram: tiempo de espera agotado',
  ),
  journalLine(0, '09:20:34', 'moss-telegram: una línea fatal de ejemplo'),
  '-- una línea sin el formato short-iso --',
  journalLine(0, '09:31:15', 'Mensaje de 123456789 respondido en 2.8 s'),
];

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

    testWidgets('Opciones abierta ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        summaries: _summaries,
        handlers: _handlers(),
      );
      await tester.tap(find.bySemanticsLabel(Strings.options).first);
      await _settle(tester);
      await _shot(tester, 'opciones-$mode');
    });

    testWidgets('Opciones cargando ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        summaries: _summaries,
        handlers: _handlers(loading: true),
      );
      await tester.tap(find.bySemanticsLabel(Strings.options).first);
      await _settle(tester);
      await _shot(tester, 'opciones-cargando-$mode');
    });

    testWidgets('búsqueda web: pastilla y dos filas ($mode)', (tester) async {
      final (app, _) = await startApp(
        tester,
        theme: theme,
        key: _key,
        conversation: _chat,
        summaries: _summaries,
      );
      app.searchOn.value = true;
      await _settle(tester);
      await _shot(tester, 'pastilla-$mode');
    });

    testWidgets('Buscar chats ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        conversation: _chat,
        summaries: _summaries,
      );
      await tester.sendKeyDownEvent(LogicalKeyboardKey.controlLeft);
      await tester.sendKeyEvent(LogicalKeyboardKey.keyK);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.controlLeft);
      await _settle(tester);
      await tester.enterText(find.byType(TextField).last, 'c++');
      await _settle(tester);
      await _shot(tester, 'buscar-$mode');
    });

    for (final (step, name) in [(0, 'menu'), (1, 'renombrar'), (2, 'borrar')]) {
      testWidgets('menú ⋯: $name ($mode)', (tester) async {
        await startApp(
          tester,
          theme: theme,
          key: _key,
          conversation: _chat,
          summaries: _summaries,
        );
        await tester.tap(
          find.bySemanticsLabel(Strings.moreOptions('Selection sort en C++')),
        );
        await _settle(tester);
        if (step == 1) await tester.tap(find.text(Strings.rename));
        if (step == 2) await tester.tap(find.text(Strings.delete));
        await _settle(tester);
        await _shot(tester, '$name-$mode');
      });
    }

    testWidgets('menú de / ($mode)', (tester) async {
      await startApp(
        tester,
        theme: theme,
        key: _key,
        conversation: _chat,
        summaries: _summaries,
        handlers: _handlers(),
      );
      await _typeInBox(tester, '/');
      await _shot(tester, 'comandos-$mode');
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

  // Configuración (G3): cada sección, Más contraste, el texto grande, la
  // ventana de descartar, un error de guardado, Avanzado y la tarjeta.
  for (final (mode, choice) in [
    ('oscuro', ThemeChoice.dark),
    ('claro', ThemeChoice.light),
  ]) {
    Future<FakeServer> config(
      WidgetTester tester, {
      String? page,
      bool highContrast = false,
      TextSize textSize = TextSize.normal,
      Size size = _size,
      Map<String, Object? Function(Map<String, dynamic>)> handlers = const {},
      List<String> addons = const [],
    }) async {
      final (_, server) = await startApp(
        tester,
        key: _key,
        size: size,
        addons: addons,
        summaries: _summaries,
        prefs: GuiPrefs(
          theme: choice,
          highContrast: highContrast,
          textSize: textSize,
        ),
        handlers: {
          'settings.provider.get': (_) => providerSettings(
            provider: 'custom:omniroute',
            withLab: true,
            keyStatus: 'guardada: …7c2b',
          ),
          'settings.model_params.get': (_) => modelParams(
            params: const {
              'temperature': 0.6,
              'top_p': 0.95,
              'reasoning': 'high',
            },
            extra: '{"chat_template_kwargs": {"clear_thinking": false}}',
          ),
          'settings.system_prompt.get': (_) => systemPrompt(
            mode: 'custom',
            text:
                'Eres un asistente para alumnos de bachillerato. Explica '
                'paso a paso, con ejemplos cortos y en español de México.',
          ),
          ...handlers,
        },
      );
      await tester.sendKeyEvent(LogicalKeyboardKey.f2);
      await tester.pumpAndSettle();
      if (page != null) {
        await tester.tap(find.text(page).first);
        await tester.pumpAndSettle();
      }
      return server;
    }

    for (final (name, page) in [
      ('proveedor', null),
      ('colores', Strings.colorsTitle),
      ('modelo', Strings.modelTitle),
      ('busqueda', Strings.webSearch),
    ]) {
      testWidgets('configuración: $name ($mode)', (tester) async {
        await config(tester, page: page);
        await _shot(tester, 'config-$name-$mode');
      });
    }

    testWidgets('configuración: más contraste ($mode)', (tester) async {
      await config(tester, page: Strings.colorsTitle, highContrast: true);
      await _shot(tester, 'config-contraste-$mode');
      await tester.tap(find.text(Strings.providerMenu).first);
      await tester.pumpAndSettle();
      await _shot(tester, 'config-contraste-proveedor-$mode');
    });

    testWidgets('configuración: texto grande en 720×520 ($mode)', (
      tester,
    ) async {
      await config(
        tester,
        textSize: TextSize.large,
        size: const Size(720, 520),
      );
      await _shot(tester, 'config-grande-$mode');
    });

    testWidgets('configuración: ¿descartar? ($mode)', (tester) async {
      await config(tester, page: Strings.colorsTitle);
      await tester.tap(find.text(Strings.highContrast));
      await tester.pump();
      await tester.sendKeyEvent(LogicalKeyboardKey.escape);
      await tester.pumpAndSettle();
      await _shot(tester, 'config-descartar-$mode');
    });

    testWidgets('configuración: error al guardar ($mode)', (tester) async {
      await config(
        tester,
        page: Strings.modelTitle,
        handlers: {
          'settings.model_params.check': (_) => {
            'field_errors': {
              'top_k': 'Debe ser un número entero entre 1 y 1000.',
            },
            'extra_error': null,
            'preview': null,
          },
        },
      );
      final topK = find.descendant(
        of: find.byWidgetPredicate((w) => w is FieldBox && w.label == 'Top K'),
        matching: find.byType(EditableText),
      );
      await tester.ensureVisible(topK);
      await tester.enterText(topK, '2000');
      await tester.pumpAndSettle(const Duration(milliseconds: 500));
      await tester.tap(find.text(Strings.save));
      await tester.pumpAndSettle();
      await _shot(tester, 'config-error-$mode');
    });

    testWidgets('configuración: avanzado con la vista previa ($mode)', (
      tester,
    ) async {
      await config(tester, page: Strings.modelTitle);
      final advanced = find.text(Strings.advanced);
      await tester.ensureVisible(advanced);
      await tester.pumpAndSettle();
      await tester.tap(advanced);
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.text(Strings.preview));
      await tester.pumpAndSettle();
      await _shot(tester, 'config-avanzado-$mode');
    });

    // Telegram (G4): la sección (arriba y completa), Windows, cada pregunta
    // y Más contraste.
    Future<FakeServer> telegram(
      WidgetTester tester, {
      Size size = _size,
      bool highContrast = false,
      Map<String, Object? Function(Map<String, dynamic>)> handlers = const {},
    }) => config(
      tester,
      page: Strings.telegramMenu,
      size: size,
      highContrast: highContrast,
      addons: ['telegram'],
      handlers: {
        ...telegramHandlers(
          invited: ['555123987', '700100200'],
          get: telegramGet(
            fields: {'invite_code': 'q7Xk2_Lm9-ZpR4tA'},
            envNote:
                'CHAT_API_KEY está definida en esta terminal, pero el servicio '
                'no la ve.',
          ),
        ),
        ...handlers,
      },
    );

    Future<void> edit(WidgetTester tester) async {
      final field = find.descendant(
        of: find.byWidgetPredicate(
          (w) => w is LabeledField && w.label == 'Mensajes por día',
        ),
        matching: find.byType(EditableText),
      );
      await tester.ensureVisible(field);
      await tester.enterText(field, '40');
      await tester.pumpAndSettle(const Duration(milliseconds: 500));
      await tester.tap(find.text(Strings.save));
      await tester.pumpAndSettle();
    }

    testWidgets('telegram ($mode)', (tester) async {
      await telegram(tester);
      await _shot(tester, 'config-telegram-$mode');
    });

    testWidgets('telegram completa ($mode)', (tester) async {
      await telegram(tester, size: const Size(1440, 2300));
      await _shot(tester, 'config-telegram-completa-$mode');
    });

    testWidgets('telegram: lista de zonas y linger ($mode)', (tester) async {
      await telegram(
        tester,
        handlers: {
          'service.status': (_) => serviceStatus(
            lingerNotice:
                'Sin linger, el bot se detiene al cerrar sesión y no arranca '
                'tras un apagón hasta que inicies sesión.',
            lingerCommand: 'loginctl enable-linger "\$USER"',
          ),
          'addons.telegram.get': (_) => telegramGet(fields: {'time_zone': ''}),
        },
      );
      await _shot(tester, 'config-telegram-linger-$mode');
      final field = find.descendant(
        of: find.byWidgetPredicate(
          (w) => w is LabeledField && w.label == 'Zona horaria',
        ),
        matching: find.byType(EditableText),
      );
      await tester.ensureVisible(field);
      await tester.enterText(field, 'america');
      await tester.pumpAndSettle();
      await _shot(tester, 'config-telegram-zonas-$mode');
    });

    testWidgets('telegram en Windows ($mode)', (tester) async {
      await config(tester, page: Strings.telegramMenu);
      await _shot(tester, 'config-telegram-windows-$mode');
    });

    testWidgets('telegram: ¿descartar llaves? ($mode)', (tester) async {
      await telegram(
        tester,
        handlers: {
          'addons.telegram.save': (_) => throw const FakeError(
            -32008,
            'confirmar',
            {'message': 'Se descartarán: rara, vieja. ¿Guardar?'},
          ),
        },
      );
      await edit(tester);
      await _shot(tester, 'config-telegram-descartar-$mode');
    });

    testWidgets('telegram: ¿reiniciar ahora? ($mode)', (tester) async {
      await telegram(
        tester,
        handlers: {
          'addons.telegram.check': (_) => {
            'field_errors': <String, String>{},
            'form_error': null,
            'needs_restart': true,
          },
          'addons.telegram.save': (_) => {'needs_restart': true},
        },
      );
      await edit(tester);
      await _shot(tester, 'config-telegram-reiniciar-$mode');
    });

    testWidgets('telegram: ¿reemplazar el servicio? ($mode)', (tester) async {
      await telegram(
        tester,
        handlers: {
          'service.apply': (_) => throw const FakeError(-32008, 'confirmar', {
            'message': '¿Reemplazar el servicio existente?',
          }),
        },
      );
      await edit(tester);
      await _shot(tester, 'config-telegram-ajena-$mode');
    });

    testWidgets('telegram: ¿quitar invitado? ($mode)', (tester) async {
      await telegram(tester);
      await tester.ensureVisible(find.text(Strings.removeInvited).last);
      await tester.pumpAndSettle();
      await tester.tap(find.text(Strings.removeInvited).last);
      await tester.pumpAndSettle();
      await _shot(tester, 'config-telegram-quitar-$mode');
    });

    testWidgets('telegram: errores de check ($mode)', (tester) async {
      await telegram(
        tester,
        handlers: {
          'addons.telegram.check': (_) => {
            'field_errors': {
              'daily_limit':
                  'El tope de mensajes por usuario por día debe ser de 1 a '
                  '10000.',
            },
            'form_error': 'Mensajes por día: …',
            'needs_restart': false,
          },
        },
      );
      await edit(tester);
      await _shot(tester, 'config-telegram-error-$mode');
    });

    // Ver registro: con errores y separadores, vacío y cargando.
    for (final (name, log)
        in <(String, Object? Function(Map<String, dynamic>))>[
          (
            'registro',
            (_) => logResult(
              lines: sampleLog(),
              errors: [7, 8, 9],
              truncated: true,
            ),
          ),
          (
            'registro-vacio',
            (_) => logResult(
              lines: [],
              notice: 'El bot todavía no ha escrito nada en el registro.',
            ),
          ),
          ('registro-cargando', (_) => noReply),
        ]) {
      testWidgets('telegram: $name ($mode)', (tester) async {
        await telegram(tester, handlers: {'service.log': log});
        await tester.tap(find.text(Strings.viewLog));
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 400));
        await tester.pump(const Duration(milliseconds: 400));
        await _shot(tester, 'config-telegram-$name-$mode');
      });
    }

    testWidgets('telegram: más contraste ($mode)', (tester) async {
      await telegram(tester, highContrast: true);
      await _shot(tester, 'config-telegram-contraste-$mode');
    });

    testWidgets('chat vacío sin configurar ($mode)', (tester) async {
      await startApp(
        tester,
        key: _key,
        summaries: _summaries,
        prefs: GuiPrefs(theme: choice),
        handlers: {
          'app.status': (_) => appStatus(
            ready: false,
            problem:
                'Falta la API key de NVIDIA NIM. Agrégala en Configuración '
                '→ Proveedor.',
          ),
        },
      );
      await tester.pump(const Duration(milliseconds: 100));
      await _shot(tester, 'vacio-sin-configurar-$mode');
    });
  }
}
