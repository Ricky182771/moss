import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:moss_gui/strings.dart';
import 'package:moss_gui/widgets/message_list.dart';
import 'package:moss_gui/widgets/options_panel.dart';

import 'fake_server.dart';
import 'harness.dart';

const _models = ['deepseek-ai/deepseek-v4', 'z-ai/glm5', 'qwen/qwen3.5-coder'];

/// Los métodos de Opciones; [extra] reemplaza los que haga falta.
Map<String, Object? Function(Map<String, dynamic>)> optionsHandlers({
  Map<String, Object? Function(Map<String, dynamic>)> extra = const {},
}) => {
  'settings.provider.get': (_) => providerSettings(),
  'commands.list': (_) => commandList(),
  'models.list': (p) => {
    'models': (p['draft'] as Map)['provider'] == 'gemini'
        ? ['gemini-3-pro', 'gemini-3-flash']
        : _models,
  },
  'settings.provider.switch': (_) => {'ready': true},
  ...extra,
};

/// Abre Opciones y deja llegar las respuestas: el clic, el cuadro que muestra
/// la ventana (OverlayPortal), el que la arma y los de cada respuesta.
Future<void> openOptions(WidgetTester tester, {bool settle = true}) async {
  await tester.tap(find.bySemanticsLabel(Strings.options).first);
  for (var i = 0; i < 4; i++) {
    await tester.pump();
  }
  if (settle) await tester.pump(const Duration(milliseconds: 200));
}

/// La caja del mensaje (el primer campo; Opciones tiene los suyos).
final _box = find.byType(TextField).first;

Map<String, dynamic> lastParams(FakeServer server, String method) =>
    server.requests.lastWhere((r) => r['method'] == method)['params']
        as Map<String, dynamic>;

void main() {
  testWidgets('lista, el actual marcado y elegir otro', (tester) async {
    final (_, server) = await startApp(tester, handlers: optionsHandlers());
    await openOptions(tester);
    expect(find.byType(OptionsPanel), findsOneWidget);
    expect(find.text('NVIDIA NIM'), findsOneWidget);
    for (final m in _models) {
      expect(find.text(m), findsOneWidget);
    }
    expect(
      tester.getSemantics(find.text('z-ai/glm5')),
      isSemantics(isSelected: true, isButton: true),
    );
    expect(find.byIcon(Icons.check_rounded), findsOneWidget);
    // El filtro deja uno, resaltado; Enter lo elige.
    await tester.enterText(
      find.descendant(
        of: find.byType(OptionsPanel),
        matching: find.byType(TextField),
      ),
      'q',
    );
    await tester.pump();
    expect(find.text('z-ai/glm5'), findsNothing);
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pumpAndSettle();
    expect(lastParams(server, 'settings.provider.switch'), {
      'provider': 'nvidia',
      'model': 'qwen/qwen3.5-coder',
    });
    expect(find.byType(OptionsPanel), findsNothing);
  });

  testWidgets('otro proveedor; el sin key, atenuado y sin elegirse', (
    tester,
  ) async {
    final (_, server) = await startApp(tester, handlers: optionsHandlers());
    await openOptions(tester);
    await tester.tap(find.text('NVIDIA NIM'));
    await tester.pump();
    expect(find.text('OmniRoute'), findsOneWidget);
    expect(find.text('sin key'), findsOneWidget);
    expect(
      tester.getSemantics(find.text('OmniRoute')),
      isSemantics(isButton: true, hasEnabledState: true, isEnabled: false),
    );
    await tester.tap(find.text('OmniRoute'));
    await tester.pump();
    expect(lastParams(server, 'models.list')['draft']['provider'], 'nvidia');

    await tester.tap(find.text('Google Gemini'));
    await tester.pump();
    await tester.pump();
    expect(lastParams(server, 'models.list'), {
      'draft': {'provider': 'gemini', 'base_url': '', 'model': ''},
    });
    expect(find.text('gemini-3-flash'), findsOneWidget);
    // Nada marcado: el actual es de NVIDIA.
    expect(find.byIcon(Icons.check_rounded), findsNothing);
    await tester.tap(find.text('gemini-3-flash'));
    await tester.pumpAndSettle();
    expect(lastParams(server, 'settings.provider.switch'), {
      'provider': 'gemini',
      'model': 'gemini-3-flash',
    });
  });

  testWidgets('"Cargando modelos…" a los 300 ms y \$/cancel al cambiar', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: optionsHandlers(extra: {'models.list': (_) => noReply}),
    );
    await openOptions(tester, settle: false);
    await tester.pump(const Duration(milliseconds: 250));
    expect(find.text(Strings.loadingModels), findsNothing);
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.text(Strings.loadingModels), findsOneWidget);

    final first = server.requests.lastWhere(
      (r) => r['method'] == 'models.list',
    );
    await tester.tap(find.text('NVIDIA NIM'));
    await tester.pump();
    await tester.tap(find.text('Google Gemini'));
    await tester.pump();
    final cancel = server.requests.lastWhere((r) => r['method'] == r'$/cancel');
    expect(cancel['params'], {'id': first['id']});
    expect(cancel.containsKey('id'), isFalse); // Es notificación.
    // La nueva carga no muestra el aviso antes de 300 ms.
    expect(find.text(Strings.loadingModels), findsNothing);
  });

  testWidgets('si models.list falla: su mensaje y el modelo a mano', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: optionsHandlers(
        extra: {
          'models.list': (_) => throw const FakeError(
            -32005,
            'ValidationFailed',
            {'message': '[error de red] Sin conexión'},
          ),
        },
      ),
    );
    await openOptions(tester);
    expect(find.text('[error de red] Sin conexión'), findsOneWidget);
    await tester.enterText(
      find.widgetWithText(TextField, Strings.typeModel),
      'mi/modelo',
    );
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();
    expect(lastParams(server, 'settings.provider.switch'), {
      'provider': 'nvidia',
      'model': 'mi/modelo',
    });
  });

  testWidgets('CHAT_MODEL y CHAT_BASE_URL: deshabilitados con la razón', (
    tester,
  ) async {
    final (_, server) = await startApp(
      tester,
      handlers: optionsHandlers(
        extra: {
          'settings.provider.get': (_) =>
              providerSettings(modelLocked: true, baseUrlLocked: true),
          'commands.list': (_) =>
              commandList(modelNote: 'el modelo está fijado por CHAT_MODEL'),
        },
      ),
    );
    await openOptions(tester);
    expect(find.text('el modelo está fijado por CHAT_MODEL'), findsOneWidget);
    expect(find.text(Strings.lockedBy('CHAT_BASE_URL')), findsOneWidget);
    await tester.tap(find.text('qwen/qwen3.5-coder'));
    await tester.tap(find.text('NVIDIA NIM'));
    await tester.pump();
    expect(server.methods, isNot(contains('settings.provider.switch')));
    expect(find.text('OmniRoute'), findsNothing);
  });

  testWidgets('Busy: el mensaje del servidor junto a la lista', (tester) async {
    await startApp(
      tester,
      handlers: optionsHandlers(
        extra: {
          'settings.provider.switch': (_) => throw const FakeError(
            -32002,
            'Busy',
            {'message': 'Espera la respuesta o cancélala con Esc.'},
          ),
        },
      ),
    );
    await openOptions(tester);
    await tester.tap(find.text('qwen/qwen3.5-coder'));
    await tester.pump();
    await tester.pump();
    expect(
      find.descendant(
        of: find.byType(OptionsPanel),
        matching: find.text('Espera la respuesta o cancélala con Esc.'),
      ),
      findsOneWidget,
    );
  });

  testWidgets('Esc cierra y el foco vuelve al engranaje', (tester) async {
    await startApp(tester, handlers: optionsHandlers());
    await openOptions(tester);
    expect(find.byType(OptionsPanel), findsOneWidget);
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(find.byType(OptionsPanel), findsNothing);
    expect(FocusManager.instance.primaryFocus?.debugLabel, Strings.options);
  });

  testWidgets('sin key de búsqueda: el interruptor con la nota', (
    tester,
  ) async {
    final (app, _) = await startApp(
      tester,
      handlers: optionsHandlers(
        extra: {
          'app.status': (_) => appStatus(hasSearchKey: false),
          'commands.list': (_) =>
              commandList(searchNote: 'falta la key de búsqueda (F2)'),
        },
      ),
    );
    await openOptions(tester);
    expect(find.text('falta la key de búsqueda (F2)'), findsOneWidget);
    await tester.tap(find.text(Strings.searchWeb));
    await tester.pump();
    expect(app.searchOn.value, isFalse);
  });

  testWidgets('búsqueda: pastilla, dos filas, chat.send con search y ×', (
    tester,
  ) async {
    final (app, server) = await startApp(tester, handlers: optionsHandlers());
    final oneRow = tester.getSize(find.byType(AnimatedSize)).height;
    await openOptions(tester);
    await tester.tap(find.text(Strings.searchWeb));
    await tester.pump();
    await tester.sendKeyEvent(LogicalKeyboardKey.escape);
    await tester.pumpAndSettle();
    expect(app.searchOn.value, isTrue);
    expect(find.text(Strings.webSearch), findsOneWidget);
    expect(
      tester.getSize(find.byType(AnimatedSize)).height,
      greaterThan(oneRow),
    );

    await tester.enterText(_box, 'clima');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    await tester.pump();
    expect(lastParams(server, 'chat.send'), {
      'conversation': 'c1',
      'text': '/buscar clima',
      'search': 'clima',
    });

    await tester.tap(find.bySemanticsLabel(Strings.removeWebSearch));
    await tester.pumpAndSettle();
    expect(app.searchOn.value, isFalse);
    expect(find.text(Strings.webSearch), findsNothing);
    expect(tester.getSize(find.byType(AnimatedSize)).height, oneRow);
  });

  testWidgets('la caja de dos filas no deja la respuesta tapada', (
    tester,
  ) async {
    final (app, _) = await startApp(
      tester,
      conversation: longChat(phase: 'streaming', inProgress: true),
    );
    final pos = tester
        .state<ScrollableState>(
          find
              .descendant(
                of: find.byType(MessageList),
                matching: find.byType(Scrollable),
              )
              .first,
        )
        .position;
    expect(pos.maxScrollExtent, greaterThan(0));
    expect(pos.pixels, pos.maxScrollExtent);
    app.searchOn.value = true;
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 200));
    await tester.pump();
    expect(pos.pixels, pos.maxScrollExtent);
  });

  testWidgets('/model sin modelo abre Opciones en los proveedores', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: optionsHandlers(
        extra: {
          'commands.run': (_) => {
            'kind': 'model_picker',
            'provider': null,
            'clear': true,
          },
        },
      ),
    );
    await tester.enterText(_box, '/model');
    await tester.sendKeyEvent(LogicalKeyboardKey.enter);
    for (var i = 0; i < 6; i++) {
      await tester.pump();
    }
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.byType(OptionsPanel), findsOneWidget);
    expect(find.text('OmniRoute'), findsOneWidget);
  });

  testWidgets('chat vacío sin configurar: el problem del servidor', (
    tester,
  ) async {
    await startApp(
      tester,
      handlers: {
        'app.status': (_) => appStatus(
          ready: false,
          problem: 'Configura un proveedor para empezar.',
        ),
      },
    );
    expect(find.text(Strings.emptyPrompt), findsOneWidget);
    expect(find.text('Configura un proveedor para empezar.'), findsOneWidget);
  });
}
